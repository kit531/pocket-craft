// Player physics against the public World API. Terrain is not assumed: the test arena is built in the air
// (y 100..102), so it works with the stub world and the real one. Columns are loaded first via waitLoaded.
#include "blocks.h"
#include "pc_math.h"
#include "player.h"
#include "test.h"
#include "test_util.h"
#include "world.h"

using namespace pc;
using pctest::TempDir;
using pctest::waitLoaded;

namespace {
constexpr float kDt = 1.0f / 60.0f;

// Stone floor at y=100 (standing height 101) for x,z in [-6,6]; a stone wall at x=4, y 101..102, z -2..2.
bool buildArena(World& world) {
    if (!waitLoaded(world, Vec3(0.5f, 101.0f, 0.5f), -6, 6, -6, 6)) return false;
    bool ok = true;
    auto put = [&](int x, int y, int z) {
        if (!world.setBlock(x, y, z, STONE)) ok = false;
    };
    for (int x = -6; x <= 6; ++x)
        for (int z = -6; z <= 6; ++z) put(x, 100, z);
    for (int y = 101; y <= 102; ++y)
        for (int z = -2; z <= 2; ++z) put(4, y, z);
    return ok;
}

void simulate(World& world, Player& p, const PlayerInput& in, float seconds) {
    const int steps = static_cast<int>(seconds / kDt);
    for (int i = 0; i < steps; ++i) {
        world.update(p.pos);
        p.update(world, in, kDt);
    }
}
}  // namespace

TEST(player_lands_on_ground) {
    TempDir dir("player_land");
    World world(1234, dir.path());
    CHECK(buildArena(world));
    Player p;
    p.pos = Vec3(0.5f, 103.0f, 0.5f);
    simulate(world, p, PlayerInput{}, 2.0f);
    CHECK_NEAR(p.pos.y, 101.0f, 1e-3f);
    CHECK(p.onGround);
}

TEST(player_cannot_walk_into_wall) {
    TempDir dir("player_wall");
    World world(1234, dir.path());
    CHECK(buildArena(world));
    Player p;
    p.pos = Vec3(0.5f, 101.0f, 0.5f);
    PlayerInput in;
    in.moveX = 1.0f;  // yaw 0: strafing right moves toward +X, into the wall at x=4
    simulate(world, p, in, 3.0f);
    // The wall face is at x=4 and the body half-width is 0.3, so the player stops at x=3.7.
    CHECK(p.pos.x <= 3.7f + 1e-3f);
    CHECK(p.pos.x > 3.0f);  // it did walk up to the wall
    CHECK_NEAR(p.pos.y, 101.0f, 1e-3f);
}

TEST(player_jump_goes_up_then_down) {
    TempDir dir("player_jump");
    World world(1234, dir.path());
    CHECK(buildArena(world));
    Player p;
    p.pos = Vec3(0.5f, 101.0f, 0.5f);
    simulate(world, p, PlayerInput{}, 0.5f);  // settle on the floor
    CHECK(p.onGround);
    const float groundY = p.pos.y;

    PlayerInput jump;
    jump.jump = true;
    world.update(p.pos);
    p.update(world, jump, kDt);  // jump held for one tick only
    CHECK(p.vel.y > 0.0f);

    float maxY = p.pos.y;
    for (int i = 0; i < 120; ++i) {  // 2 seconds: apex after ~0.34 s, landed well before the end
        world.update(p.pos);
        p.update(world, PlayerInput{}, kDt);
        if (p.pos.y > maxY) maxY = p.pos.y;
    }
    // Apex is v^2 / (2g) = 8.4^2 / 50 = 1.41 blocks above the ground.
    CHECK(maxY > groundY + 1.0f);
    CHECK(maxY < groundY + 2.0f);
    CHECK_NEAR(p.pos.y, groundY, 1e-3f);
    CHECK(p.onGround);
}
