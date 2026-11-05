// World implementation: column storage, streaming, async generation/meshing, saving.
//
// Threading: the game thread owns every Column (the `cols` map). Workers only ever see (a) a freshly created
// Column they are generating, handed back through a queue, (b) an immutable SectionSnapshot copied on the game
// thread, (c) a copy-on-unload Column being saved. Mesh results carry the section version they were built from;
// pollMeshUpdates drops any result whose version is no longer current.
#include "world.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "column.h"
#include "log.h"
#include "mesher.h"
#include "storage.h"
#include "terrain.h"
#include "thread_pool.h"

namespace pc {
namespace {

constexpr int kMaxRadius = 16;
constexpr int kInflightCap = 6;

struct GenResult {
    std::unique_ptr<Column> col;
};

struct MeshResult {
    SectionKey key;
    std::shared_ptr<ColShared> cs;  // null => pass-through (removal) update
    uint32_t ver = 0;
    std::shared_ptr<const SectionMesh> mesh;
};

// State shared between the game thread and worker jobs (kept alive by the jobs' shared_ptr).
struct Shared {
    std::string dir;
    uint32_t seed = 0;
    ThreadSafeQueue<GenResult> genDone;
    ThreadSafeQueue<MeshResult> meshDone;
    std::atomic<int> inflight{0};
    std::atomic<bool> needScan{true};
    std::mutex saveMx;  // serialises file writes
    std::mutex savingMx;
    std::unordered_map<ChunkKey, std::shared_ptr<Column>> saving;  // unloaded columns not yet on disk
};

struct Offset { int dx, dz, d2; };

}  // namespace

struct World::Impl {
    uint32_t seed;
    std::string dir;
    std::string extra;  // opaque key/value blob persisted in world.dat
    std::atomic<int> radius{8};
    bool havePos = false;
    int lastCx = 0, lastCz = 0, lastRadius = 0;
    std::unordered_map<ChunkKey, std::unique_ptr<Column>> cols;
    std::unordered_set<ChunkKey> requested;
    mutable Column* cache = nullptr;
    std::vector<Offset> offsets;
    std::shared_ptr<Shared> sh = std::make_shared<Shared>();
    ThreadPool pool{2};  // destroyed first: joins workers before the rest goes away

    Column* find(int cx, int cz) const {
        if (cache && cache->key.cx == cx && cache->key.cz == cz) return cache;
        auto it = cols.find({cx, cz});
        if (it == cols.end()) return nullptr;
        cache = it->second.get();
        return cache;
    }

    void submitGen(int cx, int cz);
    void installGenerated();
    void unloadFar(int pcx, int pcz, int r);
    void unloadColumn(std::unordered_map<ChunkKey, std::unique_ptr<Column>>::iterator it);
    bool trySubmitSection(Column& c, int sy, bool bypassCap);
    void markDirty(int cx, int cz, int sy);
    void markNeighbourhood(int cx, int cz, int lx, int ly, int lz, int sy);
    void saveColumn(Column& c);
};

void World::Impl::submitGen(int cx, int cz) {
    requested.insert({cx, cz});
    sh->inflight.fetch_add(1);
    std::shared_ptr<Shared> s = sh;
    pool.submit([s, cx, cz] {
        auto col = std::make_unique<Column>();
        col->key = {cx, cz};
        bool done = false;
        {
            std::lock_guard<std::mutex> l(s->savingMx);
            auto it = s->saving.find({cx, cz});
            if (it != s->saving.end()) {
                std::memcpy(col->blocks, it->second->blocks, sizeof(col->blocks));
                col->modified = true;  // not necessarily on disk yet
                done = true;
            }
        }
        if (!done && loadColumnFile(s->dir, cx, cz, col->blocks)) done = true;
        if (!done) generateColumn(s->seed, cx, cz, *col);
        finalizeColumn(*col);
        s->genDone.push({std::move(col)});
        s->inflight.fetch_sub(1);
        s->needScan.store(true);
    });
}

void World::Impl::installGenerated() {
    std::vector<GenResult> got;
    sh->genDone.drain(got);
    for (auto& g : got) {
        ChunkKey k = g.col->key;
        requested.erase(k);
        g.col->dirty = 0xFF;
        cols[k] = std::move(g.col);
    }
}

void World::Impl::saveColumn(Column& c) {
    std::lock_guard<std::mutex> l(sh->saveMx);
    if (saveColumnFile(dir, c.key.cx, c.key.cz, c.blocks)) c.modified = false;
    else PC_LOGW("failed to save column %d,%d", c.key.cx, c.key.cz);
}

void World::Impl::unloadColumn(std::unordered_map<ChunkKey, std::unique_ptr<Column>>::iterator it) {
    Column& c = *it->second;
    // Invalidate in-flight mesh jobs first, then tell the renderer to drop what it has.
    for (int s = 0; s < SECTIONS_PER_COLUMN; ++s) c.cs->ver[s].fetch_add(1);
    uint32_t sent = c.cs->sent.exchange(0);
    for (int s = 0; s < SECTIONS_PER_COLUMN; ++s)
        if (sent & (1u << s)) sh->meshDone.push({{c.key.cx, c.key.cz, s}, nullptr, 0, nullptr});
    if (cache == &c) cache = nullptr;
    if (c.modified) {
        std::shared_ptr<Column> keep(std::move(it->second));
        ChunkKey k = c.key;
        {
            std::lock_guard<std::mutex> l(sh->savingMx);
            sh->saving[k] = keep;
        }
        std::shared_ptr<Shared> s = sh;
        std::string d = dir;
        pool.submit([s, keep, k, d] {
            {
                std::lock_guard<std::mutex> l(s->saveMx);
                if (!saveColumnFile(d, k.cx, k.cz, keep->blocks)) PC_LOGW("failed to save column %d,%d", k.cx, k.cz);
            }
            std::lock_guard<std::mutex> l(s->savingMx);
            auto f = s->saving.find(k);
            if (f != s->saving.end() && f->second == keep) s->saving.erase(f);
        });
    }
    cols.erase(it);
}

void World::Impl::unloadFar(int pcx, int pcz, int r) {
    int lim = (r + 3) * (r + 3);
    for (auto it = cols.begin(); it != cols.end();) {
        int dx = it->first.cx - pcx, dz = it->first.cz - pcz;
        if (dx * dx + dz * dz > lim) {
            auto cur = it++;
            unloadColumn(cur);
        } else {
            ++it;
        }
    }
}

void World::Impl::markDirty(int cx, int cz, int sy) {
    if (sy < 0 || sy >= SECTIONS_PER_COLUMN) return;
    Column* c = find(cx, cz);
    if (!c) return;
    c->cs->ver[sy].fetch_add(1);
    c->dirty |= static_cast<uint8_t>(1u << sy);
}

// Marks the section containing the changed cell and every section whose 18^3 snapshot includes the cell.
void World::Impl::markNeighbourhood(int cx, int cz, int lx, int ly, int lz, int sy) {
    int xs[2] = {0, 0}, zs[2] = {0, 0}, ys[2] = {0, 0};
    int nx = 1, nz = 1, ny = 1;
    if (lx == 0) xs[nx++] = -1; else if (lx == 15) xs[nx++] = 1;
    if (lz == 0) zs[nz++] = -1; else if (lz == 15) zs[nz++] = 1;
    if (ly == 0) ys[ny++] = -1; else if (ly == 15) ys[ny++] = 1;
    for (int a = 0; a < nx; ++a)
        for (int b = 0; b < nz; ++b)
            for (int c = 0; c < ny; ++c) markDirty(cx + xs[a], cz + zs[b], sy + ys[c]);
}

bool World::Impl::trySubmitSection(Column& c, int sy, bool bypassCap) {
    uint8_t bit = static_cast<uint8_t>(1u << sy);
    if (!(c.dirty & bit)) return false;
    Column* nb[3][3];
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            nb[dz + 1][dx + 1] = (dx || dz) ? find(c.key.cx + dx, c.key.cz + dz) : &c;
            if (!nb[dz + 1][dx + 1]) return false;  // wait until all 8 neighbours are loaded
        }
    if (!bypassCap && sh->inflight.load() >= kInflightCap) return false;
    c.dirty &= static_cast<uint8_t>(~bit);
    SectionKey key{c.key.cx, c.key.cz, sy};
    if (c.nonAir[sy] == 0) {
        c.cs->ver[sy].fetch_add(1);  // drop any older in-flight mesh
        if (c.cs->sent.fetch_and(~(1u << sy)) & (1u << sy)) sh->meshDone.push({key, nullptr, 0, nullptr});
        return true;
    }
    auto snap = std::make_shared<SectionSnapshot>();
    for (int y = -1; y <= 16; ++y) {
        int gy = sy * 16 + y;
        for (int z = -1; z <= 16; ++z) {
            for (int x = -1; x <= 16; ++x) {
                int si = SectionSnapshot::idx(x, y, z);
                if (gy < 0) { snap->blocks[si] = BEDROCK; snap->sky[si] = 0; continue; }
                if (gy >= CHUNK_H) { snap->blocks[si] = AIR; snap->sky[si] = 15; continue; }
                const Column* cc = nb[z < 0 ? 0 : (z > 15 ? 2 : 1)][x < 0 ? 0 : (x > 15 ? 2 : 1)];
                int ci = Column::idx(x & 15, gy, z & 15);
                snap->blocks[si] = cc->blocks[ci];
                snap->sky[si] = cc->skyAt(ci);
            }
        }
    }
    uint32_t ver = c.cs->ver[sy].load();
    std::shared_ptr<ColShared> cs = c.cs;
    std::shared_ptr<Shared> s = sh;
    sh->inflight.fetch_add(1);
    pool.submit([s, snap, cs, key, ver] {
        MeshResult r;
        r.key = key;
        r.cs = cs;
        r.ver = ver;
        r.mesh = meshSection(*snap, key);
        s->meshDone.push(std::move(r));
        s->inflight.fetch_sub(1);
        s->needScan.store(true);
    });
    return true;
}

World::World(uint32_t seed, std::string saveDir) : impl_(new Impl) {
    impl_->seed = seed;
    impl_->dir = std::move(saveDir);
    std::error_code ec;
    std::filesystem::create_directories(impl_->dir, ec);
    uint32_t stored;
    std::string blob;
    if (readWorldDat(impl_->dir, stored, blob)) {
        if (stored != seed) PC_LOGW("world.dat seed %u overrides requested seed %u", stored, seed);
        impl_->seed = stored;
        impl_->extra = std::move(blob);
    } else {
        writeWorldDat(impl_->dir, seed, impl_->extra);
    }
    impl_->sh->dir = impl_->dir;
    impl_->sh->seed = impl_->seed;
    for (int dz = -(kMaxRadius + 3); dz <= kMaxRadius + 3; ++dz)
        for (int dx = -(kMaxRadius + 3); dx <= kMaxRadius + 3; ++dx) {
            int d2 = dx * dx + dz * dz;
            if (d2 <= (kMaxRadius + 3) * (kMaxRadius + 3)) impl_->offsets.push_back({dx, dz, d2});
        }
    std::stable_sort(impl_->offsets.begin(), impl_->offsets.end(),
                     [](const Offset& a, const Offset& b) { return a.d2 < b.d2; });
}

World::~World() = default;

uint32_t World::seed() const { return impl_->seed; }

void World::setRenderDistance(int chunks) { impl_->radius.store(std::max(2, std::min(kMaxRadius, chunks))); }

void World::update(const Vec3& p) {
    Impl& m = *impl_;
    int pcx = floorDiv(floorToInt(p.x), CHUNK_W), pcz = floorDiv(floorToInt(p.z), CHUNK_W);
    int r = m.radius.load();
    if (!m.havePos || pcx != m.lastCx || pcz != m.lastCz || r != m.lastRadius) {
        m.havePos = true;
        m.lastCx = pcx; m.lastCz = pcz; m.lastRadius = r;
        m.sh->needScan.store(true);
    }
    if (!m.sh->needScan.exchange(false)) return;
    m.installGenerated();
    m.unloadFar(pcx, pcz, r);
    int meshR2 = r * r, genR2 = (r + 2) * (r + 2);
    for (const Offset& o : m.offsets) {
        if (o.d2 > genR2) break;
        if (m.sh->inflight.load() >= kInflightCap) break;  // a finishing job re-arms needScan
        int cx = pcx + o.dx, cz = pcz + o.dz;
        Column* c = m.find(cx, cz);
        if (!c) {
            if (!m.requested.count({cx, cz})) m.submitGen(cx, cz);
            continue;
        }
        if (o.d2 <= meshR2 && c->dirty)
            for (int sy = 0; sy < SECTIONS_PER_COLUMN; ++sy) m.trySubmitSection(*c, sy, false);
    }
}

uint8_t World::getBlock(int x, int y, int z) const {
    if (y < 0) return BEDROCK;
    if (y >= CHUNK_H) return AIR;
    const Column* c = impl_->find(floorDiv(x, CHUNK_W), floorDiv(z, CHUNK_W));
    return c ? c->get(x & 15, y, z & 15) : static_cast<uint8_t>(AIR);
}

bool World::isLoaded(int cx, int cz) const { return impl_->find(cx, cz) != nullptr; }

int World::topSolidY(int x, int z) const {
    const Column* c = impl_->find(floorDiv(x, CHUNK_W), floorDiv(z, CHUNK_W));
    if (!c) return -1;
    return static_cast<int>(c->top[((z & 15) << 4) | (x & 15)]) - 1;
}

bool World::setBlock(int x, int y, int z, uint8_t id) {
    Impl& m = *impl_;
    if (y < 0 || y >= CHUNK_H || id >= BLOCK_COUNT) return false;
    int cx = floorDiv(x, CHUNK_W), cz = floorDiv(z, CHUNK_W);
    Column* c = m.find(cx, cz);
    if (!c) return false;
    int lx = x & 15, lz = z & 15, sy = y >> 4, ly = y & 15;
    int i = Column::idx(lx, y, lz);
    uint8_t old = c->blocks[i];
    if (old == id) return true;
    c->blocks[i] = id;
    c->modified = true;
    c->nonAir[sy] = static_cast<uint16_t>(c->nonAir[sy] + (id != AIR) - (old != AIR));
    uint8_t& top = c->top[(lz << 4) | lx];
    if (id != AIR && y + 1 > top) top = static_cast<uint8_t>(y + 1);
    else if (id == AIR && y + 1 == top) {
        int t = y - 1;
        while (t >= 0 && c->blocks[Column::idx(lx, t, lz)] == AIR) --t;
        top = static_cast<uint8_t>(t + 1);
    }
    m.markNeighbourhood(cx, cz, lx, ly, lz, sy);

    // Light: only recompute when the block changes how sky light passes through.
    bool lightChanged = blockOpaque(old) != blockOpaque(id) || skyAbsorb(old) != skyAbsorb(id);
    if (lightChanged) {
        static thread_local uint8_t before[kColumnCells / 2];
        std::memcpy(before, c->sky, sizeof(before));
        computeSkyLight(*c);
        for (int s = 0; s < SECTIONS_PER_COLUMN; ++s) {
            const uint8_t* a = before + s * 2048;
            const uint8_t* b = c->sky + s * 2048;
            if (std::memcmp(a, b, 2048) == 0) continue;
            // Find which border layers changed so the neighbours that sample them are re-meshed.
            bool xlo = false, xhi = false, zlo = false, zhi = false, ylo = false, yhi = false;
            for (int k = 0; k < 4096; ++k) {
                int bi = (s * 4096 + k);
                int va = (before[bi >> 1] >> ((bi & 1) * 4)) & 15;
                if (va == c->skyAt(bi)) continue;
                int cxl = k & 15, czl = (k >> 4) & 15, cyl = k >> 8;
                xlo |= cxl == 0; xhi |= cxl == 15; zlo |= czl == 0; zhi |= czl == 15; ylo |= cyl == 0; yhi |= cyl == 15;
            }
            m.markDirty(cx, cz, s);
            if (xlo) m.markDirty(cx - 1, cz, s);
            if (xhi) m.markDirty(cx + 1, cz, s);
            if (zlo) m.markDirty(cx, cz - 1, s);
            if (zhi) m.markDirty(cx, cz + 1, s);
            if (ylo) m.markDirty(cx, cz, s - 1);
            if (yhi) m.markDirty(cx, cz, s + 1);
        }
    }
    // Edits are urgent: submit now (bypassing the streaming cap) for every dirty section of nearby columns.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            Column* n = m.find(cx + dx, cz + dz);
            if (n && n->dirty)
                for (int s = 0; s < SECTIONS_PER_COLUMN; ++s) m.trySubmitSection(*n, s, true);
        }
    return true;
}

size_t World::pollMeshUpdates(std::vector<MeshUpdate>& out) {
    Impl& m = *impl_;
    std::vector<MeshResult> got;
    m.sh->meshDone.drain(got);
    size_t n = 0;
    for (auto& r : got) {
        if (!r.cs) {  // removal
            out.push_back({r.key, nullptr});
            ++n;
            continue;
        }
        if (r.cs->ver[r.key.sy].load() != r.ver) continue;  // stale
        uint32_t bit = 1u << r.key.sy;
        if (r.mesh) r.cs->sent.fetch_or(bit);
        else if (!(r.cs->sent.fetch_and(~bit) & bit)) continue;  // nothing on the renderer, nothing to remove
        out.push_back({r.key, r.mesh});
        ++n;
    }
    return n;
}

void World::remeshAll() {
    Impl& m = *impl_;
    for (auto& kv : m.cols) {
        kv.second->cs->sent.store(0);
        kv.second->dirty = 0xFF;
        for (int s = 0; s < SECTIONS_PER_COLUMN; ++s) kv.second->cs->ver[s].fetch_add(1);
    }
    m.sh->needScan.store(true);
}

void World::saveAll() {
    Impl& m = *impl_;
    for (auto& kv : m.cols)
        if (kv.second->modified) m.saveColumn(*kv.second);
    std::vector<std::shared_ptr<Column>> pending;
    {
        std::lock_guard<std::mutex> l(m.sh->savingMx);
        for (auto& kv : m.sh->saving) pending.push_back(kv.second);
    }
    for (auto& c : pending) {
        std::lock_guard<std::mutex> l(m.sh->saveMx);
        saveColumnFile(m.dir, c->key.cx, c->key.cz, c->blocks);
    }
    std::lock_guard<std::mutex> l(m.sh->saveMx);
    writeWorldDat(m.dir, m.seed, m.extra);
}

}  // namespace pc
