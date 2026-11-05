// Game-level tests with a fake renderer (provides createVulkanRenderer for the host build).
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "blocks.h"
#include "game.h"
#include "renderer.h"
#include "test.h"
#include "test_util.h"

using namespace pc;

namespace {
std::atomic<int> gFrames{0};
std::atomic<uint32_t> gParticles{0}, gMaxParticles{0};

class FakeRenderer : public IRenderer {
public:
    bool init(void*) override { return true; }
    void resize(int, int) override {}
    void uploadMesh(const SectionMesh&) override {}
    void removeMesh(const SectionKey&) override {}
    void render(const FrameParams& f) override {
        gParticles = f.particleCount;
        if (f.particleCount > gMaxParticles) gMaxParticles = f.particleCount;
        ++gFrames;
    }
    void shutdown() override {}
};

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

namespace pc {
std::unique_ptr<IRenderer> createVulkanRenderer() { return std::make_unique<FakeRenderer>(); }
}  // namespace pc

TEST(game_hotbar_roundtrip_and_persistence) {
    pctest::TempDir dir("game_hotbar");
    {
        Game g;
        g.onCreate(dir.path(), "w", 7);
        for (int i = 0; i < 9; ++i) CHECK(g.getHotbarSlot(i) == kHotbarBlocks[i]);
        g.setHotbarSlot(0, STONE);
        CHECK(g.getHotbarSlot(0) == STONE);
        g.setHotbarSlot(1, AIR);
        g.setHotbarSlot(1, WATER);
        g.setHotbarSlot(1, BEDROCK);
        g.setHotbarSlot(1, 200);
        g.setHotbarSlot(1, -3);
        g.setHotbarSlot(9, STONE);
        g.setHotbarSlot(-1, STONE);
        CHECK(g.getHotbarSlot(1) == kHotbarBlocks[1]);
        CHECK(g.getHotbarSlot(9) == 0);
        g.setHotbarSlot(8, SAND);
        g.onDestroy();
    }
    Game g2;
    g2.onCreate(dir.path(), "w", 7);
    CHECK(g2.getHotbarSlot(0) == STONE);
    CHECK(g2.getHotbarSlot(8) == SAND);
    CHECK(g2.getHotbarSlot(2) == kHotbarBlocks[2]);
}

TEST(game_break_spawns_particles_that_die) {
    pctest::TempDir dir("game_particles");
    Game g;
    g.onCreate(dir.path(), "w", 99);
    g.onSurfaceCreated(reinterpret_cast<void*>(1));
    g.onSurfaceChanged(320, 240);
    g.onResume();
    InputState in;
    in.lookDY = 2000;  // look straight down at the ground under the player
    g.setInput(in);
    in.lookDY = 0;
    // Pulse break until particles show up (the world needs time to load and the player to spawn).
    bool seen = false;
    for (int i = 0; i < 400 && !seen; ++i) {
        in.breakPressed = true;
        g.setInput(in);
        sleepMs(20);
        in.breakPressed = false;
        g.setInput(in);
        sleepMs(30);
        seen = gMaxParticles > 0;
    }
    CHECK(seen);
    CHECK(gMaxParticles <= kMaxParticles);
    // Stop breaking; every particle must be gone within 1.5 s.
    sleepMs(1700);
    CHECK(gParticles == 0);
    CHECK(gFrames > 0);
    g.onSurfaceDestroyed();
    g.onDestroy();
}
