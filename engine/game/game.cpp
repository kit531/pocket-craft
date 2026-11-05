#include "game.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include "blocks.h"
#include "pc_log.h"
#include "player.h"
#include "raycast.h"
#include "renderer.h"
#include "world.h"

namespace pc {
namespace {
using Clock = std::chrono::steady_clock;
constexpr float kTick = 1.0f / 60.0f;
constexpr int kMaxTicksPerFrame = 5;
constexpr size_t kMaxUploadsPerFrame = 12;  // spread GPU uploads over frames to avoid hitches
constexpr float kRepeatInterval = 0.25f;  // hold-to-break / hold-to-place period
constexpr float kBobAmplitude = 0.04f;
constexpr float kParticleGravity = 16.0f;

struct Particle {
    Vec3 pos, vel;
    float age, life, size;
    uint16_t tile;
    uint8_t corner;
};
}  // namespace

struct Game::Impl {
    Impl() { for (int i = 0; i < 9; ++i) hotbar[i] = kHotbarBlocks[i]; }

    // lifecycle (UI thread, guarded by lifeMutex)
    std::mutex lifeMutex;
    std::unique_ptr<World> world;
    std::thread thread;
    std::atomic<bool> stop{false};
    void* window = nullptr;
    bool created = false, resumed = false;
    std::atomic<int> width{0}, height{0};

    // input (written by UI thread, consumed by game thread)
    std::mutex inMutex;
    InputState in, prevIn;
    float lookDX = 0, lookDY = 0;
    bool pendBreak = false, pendPlace = false, pendFly = false;
    float breakTimer = 0, placeTimer = 0;  // game thread: seconds until the next repeat while held

    // hotbar: block id per slot (any thread)
    std::atomic<uint8_t> hotbar[9];

    // break particles: fixed pool, swap-remove, game thread only
    Particle particles[kMaxParticles];
    ParticleInstance instances[kMaxParticles];
    uint32_t particleCount = 0;
    uint32_t rng = 0x9E3779B9u;
    float bobPhase = 0, bobAmp = 0;

    // settings
    std::atomic<int> renderDistance{8};
    std::atomic<float> sensitivity{0.004f}, fovDeg{70.0f};
    std::atomic<int> frameCap{60};

    // game-thread state (also touched by lifecycle methods only while the thread is stopped)
    Player player;
    bool spawned = false;
    std::unique_ptr<IRenderer> renderer;
    std::deque<MeshUpdate> uploads;
    mutable std::mutex dbgMutex;
    std::string dbg;

    float rnd() {  // 0..1
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return static_cast<float>(rng >> 8) * (1.0f / 16777216.0f);
    }
    float rnd(float lo, float hi) { return lo + (hi - lo) * rnd(); }
    void spawnBreakParticles(int x, int y, int z, uint8_t id);
    void updateParticles(float dt);
    void updateBob(float dt);
    uint8_t lightAt(const Vec3&) const { return 15; }  // World exposes no light data yet
    void reconcile();  // caller holds lifeMutex
    void run();
    void frame(float dt, float& acc, float timeSeconds);
    void tick();
    void doActions(float dt);
    void flushMeshes();
    void loadPlayer();
    void savePlayer();
};

// world.dat blob: "P x y z yaw pitch" (player, optional) and "H b0,b1,..,b8" (hotbar, optional), space separated.
void Game::Impl::loadPlayer() {
    const std::string& ex = world->getExtra();
    float x, y, z, yaw, pitch;
    if (std::sscanf(ex.c_str(), "P %f %f %f %f %f", &x, &y, &z, &yaw, &pitch) == 5 && y > -10 && y < CHUNK_H + 64) {
        player.pos = {x, y, z};
        player.yaw = yaw;
        player.pitch = pitch;
        spawned = true;  // skip the spawn search; physics waits until the column is loaded
    }
    size_t h = ex.find("H ");
    if (h != std::string::npos && (h == 0 || ex[h - 1] == ' ')) {
        int v[9];
        if (std::sscanf(ex.c_str() + h, "H %d,%d,%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                        &v[7], &v[8]) == 9) {
            for (int i = 0; i < 9; ++i)
                if (isPlaceable(static_cast<uint8_t>(v[i])) && v[i] < 256) hotbar[i] = static_cast<uint8_t>(v[i]);
        }
    }
}

void Game::Impl::savePlayer() {
    char buf[192];
    int n = 0;
    if (spawned)
        n = std::snprintf(buf, sizeof buf, "P %.3f %.3f %.3f %.4f %.4f ", player.pos.x, player.pos.y, player.pos.z,
                          player.yaw, player.pitch);
    std::snprintf(buf + n, sizeof buf - n, "H %d,%d,%d,%d,%d,%d,%d,%d,%d", hotbar[0].load(), hotbar[1].load(),
                  hotbar[2].load(), hotbar[3].load(), hotbar[4].load(), hotbar[5].load(), hotbar[6].load(),
                  hotbar[7].load(), hotbar[8].load());
    world->setExtra(buf);
}

void Game::Impl::spawnBreakParticles(int x, int y, int z, uint8_t id) {
    int n = 16 + static_cast<int>(rnd() * 9.0f);
    for (int i = 0; i < n && particleCount < kMaxParticles; ++i) {
        Particle& p = particles[particleCount++];
        p.pos = {x + rnd(), y + rnd(), z + rnd()};
        float ang = rnd(0, 2 * kPi), sp = rnd(0.5f, 2.5f);
        p.vel = {std::cos(ang) * sp, rnd(1.0f, 4.0f), std::sin(ang) * sp};
        p.age = 0;
        p.life = rnd(0.6f, 1.2f);
        p.size = rnd(0.08f, 0.14f);
        p.tile = blockInfo(id).tileSide;
        p.corner = static_cast<uint8_t>(rnd() * 4.0f) & 3;
    }
}

void Game::Impl::updateParticles(float dt) {
    World& w = *world;
    auto solidAt = [&w](const Vec3& q) { return blockInfo(w.getBlock(floorToInt(q.x), floorToInt(q.y), floorToInt(q.z))).solid; };
    for (uint32_t i = 0; i < particleCount;) {
        Particle& p = particles[i];
        p.age += dt;
        if (p.age >= p.life) {
            p = particles[--particleCount];
            continue;
        }
        p.vel.y -= kParticleGravity * dt;
        Vec3 n = p.pos;
        n.y += p.vel.y * dt;
        if (solidAt(n)) {
            if (p.vel.y < 0) { p.vel.y = -p.vel.y * 0.25f; if (p.vel.y < 0.5f) p.vel.y = 0; }
            else p.vel.y = 0;
            p.vel.x *= 0.6f; p.vel.z *= 0.6f;
        } else {
            p.pos.y = n.y;
        }
        n = p.pos; n.x += p.vel.x * dt;
        if (solidAt(n)) p.vel.x = -p.vel.x * 0.3f; else p.pos.x = n.x;
        n = p.pos; n.z += p.vel.z * dt;
        if (solidAt(n)) p.vel.z = -p.vel.z * 0.3f; else p.pos.z = n.z;
        ++i;
    }
}

void Game::Impl::updateBob(float dt) {
    float speed = std::sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z);
    bool walking = player.onGround && !player.flying && !player.inWater;
    float target = walking ? kBobAmplitude * clampf(speed / 4.3f, 0.0f, 1.0f) : 0.0f;
    bobAmp += (target - bobAmp) * clampf(dt * 12.0f, 0.0f, 1.0f);
    if (walking) bobPhase += speed * dt * 2.4f;
    if (bobPhase > 2000.0f * kPi) bobPhase -= 2000.0f * kPi;
}

void Game::Impl::reconcile() {
    bool want = created && window && resumed && width > 0 && height > 0;
    if (want && !thread.joinable()) {
        stop = false;
        thread = std::thread([this] { run(); });
    } else if (!want && thread.joinable()) {
        stop = true;
        thread.join();
    }
}

void Game::Impl::run() {
    renderer = createVulkanRenderer();
    if (!renderer || !renderer->init(window)) {
        PC_LOGE("renderer init failed");
        renderer.reset();
        return;
    }
    int w = width, h = height;
    renderer->resize(w, h);
    uploads.clear();
    world->remeshAll();  // GPU buffers are new
    world->setRenderDistance(renderDistance);

    auto start = Clock::now(), last = start;
    int fpsFrames = 0;
    auto fpsT = start;
    float fps = 0, acc = 0;
    while (!stop) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        if (w != width || h != height) {
            w = width; h = height;
            renderer->resize(w, h);
        }
        acc += dt;
        frame(dt, acc, std::chrono::duration<float>(now - start).count());

        ++fpsFrames;
        float el = std::chrono::duration<float>(now - fpsT).count();
        if (el >= 1.0f) {
            fps = fpsFrames / el;
            fpsFrames = 0;
            fpsT = now;
            RenderStats rs = renderer->stats();
            char buf[160];
            std::snprintf(buf, sizeof buf, "%.0f fps  pos %.1f %.1f %.1f  sections %d/%d  gpu %zu KB", fps,
                          player.pos.x, player.pos.y, player.pos.z, rs.sectionsDrawn, rs.sectionsTotal,
                          rs.gpuBytes / 1024);
            std::lock_guard<std::mutex> l(dbgMutex);
            dbg = buf;
        }
        // Cap at 60 (or 30 in battery saver) FPS even on 120 Hz panels; no-op when vsync already paces us.
        std::this_thread::sleep_until(now + std::chrono::microseconds(1000000 / frameCap.load()));
    }
    renderer->shutdown();
    renderer.reset();
}

void Game::Impl::tick() {
    world->update(player.pos);
    updateParticles(kTick);
    if (!spawned) {
        if (!world->isLoaded(0, 0)) return;
        int top = world->topSolidY(0, 0);
        player.pos = {0.5f, static_cast<float>(top + 2), 0.5f};
        spawned = true;
    }
    if (!world->isLoaded(floorDiv(floorToInt(player.pos.x), CHUNK_W), floorDiv(floorToInt(player.pos.z), CHUNK_W))) return;
    PlayerInput pi;
    {
        std::lock_guard<std::mutex> l(inMutex);
        pi.moveX = in.moveX; pi.moveZ = in.moveZ; pi.jump = in.jump; pi.sneak = in.sneak;
    }
    player.update(*world, pi, kTick);
    updateBob(kTick);
    if (player.pos.y < -30) spawned = false;  // fell out of the world: respawn
}

void Game::Impl::doActions(float dt) {
    bool brk, plc, fly, heldBrk, heldPlc;
    int slot;
    float dx, dy;
    {
        std::lock_guard<std::mutex> l(inMutex);
        brk = pendBreak; plc = pendPlace; fly = pendFly;
        heldBrk = in.breakPressed; heldPlc = in.placePressed;
        pendBreak = pendPlace = pendFly = false;
        dx = lookDX; dy = lookDY;
        lookDX = lookDY = 0;
        slot = in.hotbarSlot < 0 ? 0 : (in.hotbarSlot > 8 ? 8 : in.hotbarSlot);
    }
    float s = sensitivity;
    player.yaw += dx * s;
    player.pitch = clampf(player.pitch - dy * s, -1.55f, 1.55f);
    if (fly) { player.flying = !player.flying; player.vel.y = 0; }
    // Edge = immediate action; a level that stays held repeats every kRepeatInterval.
    if (brk) breakTimer = kRepeatInterval;
    else if (heldBrk) { breakTimer -= dt; if (breakTimer <= 0) { brk = true; breakTimer += kRepeatInterval; } }
    else breakTimer = 0;
    if (plc) placeTimer = kRepeatInterval;
    else if (heldPlc) { placeTimer -= dt; if (placeTimer <= 0) { plc = true; placeTimer += kRepeatInterval; } }
    else placeTimer = 0;
    if (!spawned || (!brk && !plc)) return;

    World& w = *world;
    RaycastHit hit = raycast(player.eye(), player.forward(), Player::kReach,
                             [&w](int x, int y, int z) { return w.getBlock(x, y, z); });
    if (!hit.hit) return;
    if (brk) {
        uint8_t old = w.getBlock(hit.block.x, hit.block.y, hit.block.z);
        if (old != BEDROCK && old != AIR && w.setBlock(hit.block.x, hit.block.y, hit.block.z, AIR))
            spawnBreakParticles(hit.block.x, hit.block.y, hit.block.z, old);
    } else if (hit.normal != Vec3i{}) {
        Vec3i c = hit.block + hit.normal;
        uint8_t cid = w.getBlock(c.x, c.y, c.z);
        if (c.y >= 0 && c.y < CHUNK_H && (cid == AIR || blockInfo(cid).liquid) && !player.intersectsBlock(c.x, c.y, c.z))
            w.setBlock(c.x, c.y, c.z, hotbar[slot].load());
    }
}

void Game::Impl::flushMeshes() {
    std::vector<MeshUpdate> fresh;
    world->pollMeshUpdates(fresh);
    for (auto& u : fresh) uploads.push_back(std::move(u));
    for (size_t n = 0; n < kMaxUploadsPerFrame && !uploads.empty(); ++n) {
        MeshUpdate& u = uploads.front();
        if (u.mesh && !u.mesh->empty()) renderer->uploadMesh(*u.mesh);
        else renderer->removeMesh(u.key);
        uploads.pop_front();
    }
}

void Game::Impl::frame(float dt, float& acc, float timeSeconds) {
    int ticks = 0;
    while (acc >= kTick && ticks < kMaxTicksPerFrame) {
        tick();
        acc -= kTick;
        ++ticks;
    }
    if (ticks == kMaxTicksPerFrame) acc = 0;
    doActions(dt);
    flushMeshes();

    float viewDist = renderDistance * static_cast<float>(CHUNK_W);
    FrameParams fp;
    Vec3 eye = player.eye();
    Vec3 camEye = eye;
    camEye.y += std::sin(bobPhase) * bobAmp;
    fp.cameraPos = camEye;
    fp.view = Mat4::lookAt(camEye, camEye + player.forward(), {0, 1, 0});
    float aspect = static_cast<float>(width) / static_cast<float>(height.load() > 0 ? height.load() : 1);
    fp.proj = Mat4::perspective(fovDeg * kPi / 180.0f, aspect, 0.1f, viewDist + 48.0f);
    fp.viewProj = fp.proj * fp.view;
    fp.fogEnd = viewDist - 8.0f;
    fp.fogStart = fp.fogEnd * 0.6f;
    fp.timeSeconds = std::fmod(timeSeconds, 3600.0f);
    for (uint32_t i = 0; i < particleCount; ++i) {
        const Particle& p = particles[i];
        float remain = p.life - p.age;
        ParticleInstance& o = instances[i];
        o.pos = p.pos;
        o.size = p.size * (remain < 0.2f ? remain / 0.2f : 1.0f);
        o.tile = p.tile;
        o.light = lightAt(p.pos);
        o.corner = p.corner;
    }
    fp.particles = instances;
    fp.particleCount = particleCount;
    if (spawned) {
        World& w = *world;
        RaycastHit hit = raycast(eye, player.forward(), Player::kReach,
                                 [&w](int x, int y, int z) { return w.getBlock(x, y, z); });
        fp.hasSelection = hit.hit;
        fp.selectedBlock = hit.block;
    }
    renderer->render(fp);
}

Game::Game() : impl_(new Impl) {}
Game::~Game() { onDestroy(); }

void Game::onCreate(const std::string& filesDir, const std::string& worldName, uint32_t seed) {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    if (impl_->created) return;
    impl_->world.reset(new World(seed, filesDir + "/worlds/" + worldName));
    impl_->world->setRenderDistance(impl_->renderDistance);
    impl_->loadPlayer();
    impl_->created = true;
}

void Game::onSurfaceCreated(void* nativeWindow) {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    impl_->window = nativeWindow;
    impl_->reconcile();
}

void Game::onSurfaceChanged(int w, int h) {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    impl_->width = w;
    impl_->height = h;
    impl_->reconcile();
}

void Game::onSurfaceDestroyed() {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    impl_->window = nullptr;
    impl_->reconcile();
}

void Game::onPause() {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    impl_->resumed = false;
    impl_->reconcile();
    if (impl_->world) {
        impl_->savePlayer();
        impl_->world->saveAll();
    }
}

void Game::onResume() {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    impl_->resumed = true;
    impl_->reconcile();
}

void Game::onDestroy() {
    std::lock_guard<std::mutex> l(impl_->lifeMutex);
    impl_->created = false;
    impl_->reconcile();
    if (impl_->world) {
        impl_->savePlayer();
        impl_->world->saveAll();
        impl_->world.reset();
    }
}

void Game::setInput(const InputState& s) {
    std::lock_guard<std::mutex> l(impl_->inMutex);
    impl_->lookDX += s.lookDX;
    impl_->lookDY += s.lookDY;
    if (s.breakPressed && !impl_->prevIn.breakPressed) impl_->pendBreak = true;
    if (s.placePressed && !impl_->prevIn.placePressed) impl_->pendPlace = true;
    if (s.flyToggle && !impl_->prevIn.flyToggle) impl_->pendFly = true;
    impl_->in = s;
    impl_->prevIn = s;
}

void Game::setSetting(int key, float value) {
    switch (key) {
        case SETTING_RENDER_DISTANCE: {
            int d = static_cast<int>(clampf(value, 2, 16));
            impl_->renderDistance = d;
            std::lock_guard<std::mutex> l(impl_->lifeMutex);
            if (impl_->world) impl_->world->setRenderDistance(d);
            break;
        }
        case SETTING_LOOK_SENSITIVITY: impl_->sensitivity = value; break;
        case SETTING_FOV: impl_->fovDeg = clampf(value, 50, 110); break;
        case SETTING_FRAME_CAP: impl_->frameCap = value < 45 ? 30 : 60; break;
        default: break;
    }
}

void Game::setHotbarSlot(int slot, int blockId) {
    if (slot < 0 || slot > 8 || blockId < 0 || blockId > 255 || !isPlaceable(static_cast<uint8_t>(blockId))) return;
    impl_->hotbar[slot] = static_cast<uint8_t>(blockId);
}

int Game::getHotbarSlot(int slot) const {
    return slot < 0 || slot > 8 ? 0 : impl_->hotbar[slot].load();
}

std::string Game::debugInfo() const {
    std::lock_guard<std::mutex> l(impl_->dbgMutex);
    return impl_->dbg;
}

}  // namespace pc
