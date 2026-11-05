#include "game.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#include "blocks.h"
#include "log.h"
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
}  // namespace

struct Game::Impl {
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

    // settings
    std::atomic<int> renderDistance{8};
    std::atomic<float> sensitivity{0.004f}, fovDeg{70.0f};

    // game-thread state (also touched by lifecycle methods only while the thread is stopped)
    Player player;
    bool spawned = false;
    std::unique_ptr<IRenderer> renderer;
    std::deque<MeshUpdate> uploads;
    mutable std::mutex dbgMutex;
    std::string dbg;

    void reconcile();  // caller holds lifeMutex
    void run();
    void frame(float dt, float& acc, float timeSeconds);
    void tick();
    void doActions();
    void flushMeshes();
};

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
        // Cap at 60 FPS even on 120 Hz panels (battery); no-op when vsync already paces us.
        std::this_thread::sleep_until(now + std::chrono::microseconds(16667));
    }
    renderer->shutdown();
    renderer.reset();
}

void Game::Impl::tick() {
    world->update(player.pos);
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
    if (player.pos.y < -30) spawned = false;  // fell out of the world: respawn
}

void Game::Impl::doActions() {
    bool brk, plc, fly;
    int slot;
    float dx, dy;
    {
        std::lock_guard<std::mutex> l(inMutex);
        brk = pendBreak; plc = pendPlace; fly = pendFly;
        pendBreak = pendPlace = pendFly = false;
        dx = lookDX; dy = lookDY;
        lookDX = lookDY = 0;
        slot = in.hotbarSlot < 0 ? 0 : (in.hotbarSlot > 8 ? 8 : in.hotbarSlot);
    }
    float s = sensitivity;
    player.yaw += dx * s;
    player.pitch = clampf(player.pitch - dy * s, -1.55f, 1.55f);
    if (fly) { player.flying = !player.flying; player.vel.y = 0; }
    if (!spawned || (!brk && !plc)) return;

    World& w = *world;
    RaycastHit hit = raycast(player.eye(), player.forward(), Player::kReach,
                             [&w](int x, int y, int z) { return w.getBlock(x, y, z); });
    if (!hit.hit) return;
    if (brk) {
        if (w.getBlock(hit.block.x, hit.block.y, hit.block.z) != BEDROCK)
            w.setBlock(hit.block.x, hit.block.y, hit.block.z, AIR);
    } else if (hit.normal != Vec3i{}) {
        Vec3i c = hit.block + hit.normal;
        uint8_t cid = w.getBlock(c.x, c.y, c.z);
        if (c.y >= 0 && c.y < CHUNK_H && (cid == AIR || blockInfo(cid).liquid) && !player.intersectsBlock(c.x, c.y, c.z))
            w.setBlock(c.x, c.y, c.z, kHotbarBlocks[slot]);
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

void Game::Impl::frame(float, float& acc, float timeSeconds) {
    int ticks = 0;
    while (acc >= kTick && ticks < kMaxTicksPerFrame) {
        tick();
        acc -= kTick;
        ++ticks;
    }
    if (ticks == kMaxTicksPerFrame) acc = 0;
    doActions();
    flushMeshes();

    float viewDist = renderDistance * static_cast<float>(CHUNK_W);
    FrameParams fp;
    Vec3 eye = player.eye();
    fp.cameraPos = eye;
    fp.view = Mat4::lookAt(eye, eye + player.forward(), {0, 1, 0});
    float aspect = static_cast<float>(width) / static_cast<float>(height.load() > 0 ? height.load() : 1);
    fp.proj = Mat4::perspective(fovDeg * kPi / 180.0f, aspect, 0.1f, viewDist + 48.0f);
    fp.viewProj = fp.proj * fp.view;
    fp.fogEnd = viewDist - 8.0f;
    fp.fogStart = fp.fogEnd * 0.6f;
    fp.timeSeconds = timeSeconds;
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
    if (impl_->world) impl_->world->saveAll();
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
        default: break;
    }
}

std::string Game::debugInfo() const {
    std::lock_guard<std::mutex> l(impl_->dbgMutex);
    return impl_->dbg;
}

}  // namespace pc
