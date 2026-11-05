#include "mesher.h"

#include <vector>

#include "blocks.h"

namespace pc {
namespace {

// Corner offsets (0/1 along x,y,z) for corners 0..3 of each face, see header comment for orientation.
const int8_t kCorner[6][4][3] = {
    /* +X */ {{1, 1, 1}, {1, 1, 0}, {1, 0, 0}, {1, 0, 1}},
    /* -X */ {{0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {0, 0, 0}},
    /* +Y */ {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},
    /* -Y */ {{0, 0, 1}, {1, 0, 1}, {1, 0, 0}, {0, 0, 0}},
    /* +Z */ {{0, 1, 1}, {1, 1, 1}, {1, 0, 1}, {0, 0, 1}},
    /* -Z */ {{1, 1, 0}, {0, 1, 0}, {0, 0, 0}, {1, 0, 0}},
};
const int kNormal[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

struct Scratch {
    std::vector<MeshVertex> v[2];
    std::vector<uint16_t> i[2];
    void clear() { for (int k = 0; k < 2; ++k) { v[k].clear(); i[k].clear(); } }
};

}  // namespace

std::shared_ptr<SectionMesh> meshSection(const SectionSnapshot& s, SectionKey key) {
    static thread_local Scratch sc;
    sc.clear();
    bool opaque[256], translucent[256];
    for (int b = 0; b < 256; ++b) {
        const BlockInfo& bi = blockInfo(static_cast<uint8_t>(b));
        opaque[b] = !bi.transparent;
        translucent[b] = bi.liquid || b == GLASS;
    }

    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                int ci = SectionSnapshot::idx(x, y, z);
                uint8_t b = s.blocks[ci];
                if (b == AIR) continue;
                int layer = translucent[b] ? 1 : 0;
                bool isWater = blockInfo(b).liquid;
                for (int f = 0; f < 6; ++f) {
                    const int* n = kNormal[f];
                    int nx = x + n[0], ny = y + n[1], nz = z + n[2];
                    uint8_t nb = s.blocks[SectionSnapshot::idx(nx, ny, nz)];
                    if (opaque[nb] || nb == b) continue;

                    // Tangent axes of this face for AO sampling.
                    int axis = n[0] ? 0 : (n[1] ? 1 : 2);
                    int ta = (axis + 1) % 3, tb = (axis + 2) % 3;
                    int base[3] = {nx, ny, nz};
                    MeshVertex quad[4];
                    int aoV[4];
                    bool lowerTop = isWater && s.blocks[SectionSnapshot::idx(x, y + 1, z)] != b;
                    uint16_t tileId = tileForFace(b, f);
                    for (int c = 0; c < 4; ++c) {
                        const int8_t* co = kCorner[f][c];
                        int sa = co[ta] ? 1 : -1, sb = co[tb] ? 1 : -1;
                        int p1[3] = {base[0], base[1], base[2]}, p2[3] = {base[0], base[1], base[2]};
                        int pcn[3] = {base[0], base[1], base[2]};
                        p1[ta] += sa; p2[tb] += sb; pcn[ta] += sa; pcn[tb] += sb;
                        int i1 = SectionSnapshot::idx(p1[0], p1[1], p1[2]);
                        int i2 = SectionSnapshot::idx(p2[0], p2[1], p2[2]);
                        int ic = SectionSnapshot::idx(pcn[0], pcn[1], pcn[2]);
                        bool o1 = opaque[s.blocks[i1]], o2 = opaque[s.blocks[i2]], oc = opaque[s.blocks[ic]];
                        int ao = (o1 && o2) ? 3 : (o1 + o2 + oc);
                        // Smooth sky light: average of the open cells around the vertex.
                        int sum = s.sky[SectionSnapshot::idx(nx, ny, nz)], cnt = 1;
                        if (!o1) { sum += s.sky[i1]; ++cnt; }
                        if (!o2) { sum += s.sky[i2]; ++cnt; }
                        if (!oc && !(o1 && o2)) { sum += s.sky[ic]; ++cnt; }
                        int light = (sum * 2 + cnt) / (cnt * 2);
                        if (layer == 1) ao = 0;
                        aoV[c] = ao;
                        int py = co[1] * 16;
                        if (lowerTop && co[1]) py = 14;
                        MeshVertex& v = quad[c];
                        v.x = static_cast<uint16_t>((x + co[0]) * 16);
                        v.y = static_cast<uint16_t>(y * 16 + py);
                        v.z = static_cast<uint16_t>((z + co[2]) * 16);
                        v.tileFace = packTileFace(tileId, c, f);
                        v.light = static_cast<uint8_t>(light << 4);
                        v.ao = static_cast<uint8_t>(ao);
                        v.flags = 0;
                    }
                    auto& vv = sc.v[layer];
                    auto& ii = sc.i[layer];
                    uint16_t o = static_cast<uint16_t>(vv.size());
                    for (int c = 0; c < 4; ++c) vv.push_back(quad[c]);
                    if (aoV[0] + aoV[2] > aoV[1] + aoV[3]) {
                        const uint16_t t[6] = {1, 2, 3, 1, 3, 0};
                        for (uint16_t k : t) ii.push_back(static_cast<uint16_t>(o + k));
                    } else {
                        const uint16_t t[6] = {0, 1, 2, 0, 2, 3};
                        for (uint16_t k : t) ii.push_back(static_cast<uint16_t>(o + k));
                    }
                }
            }

    if (sc.i[0].empty() && sc.i[1].empty()) return nullptr;
    auto m = std::make_shared<SectionMesh>();
    m->key = key;
    m->opaque.vertices.assign(sc.v[0].begin(), sc.v[0].end());
    m->opaque.indices.assign(sc.i[0].begin(), sc.i[0].end());
    m->translucent.vertices.assign(sc.v[1].begin(), sc.v[1].end());
    m->translucent.indices.assign(sc.i[1].begin(), sc.i[1].end());
    return m;
}

}  // namespace pc
