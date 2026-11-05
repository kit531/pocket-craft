#include "blocks.h"
#include "raycast.h"
#include "test.h"

using namespace pc;

namespace {
// One stone block at (5,100,5) in an otherwise empty world.
struct OneBlock {
    uint8_t operator()(int x, int y, int z) const {
        return (x == 5 && y == 100 && z == 5) ? STONE : AIR;
    }
};
}  // namespace

TEST(raycast_hits_west_face) {
    RaycastHit r = raycast(Vec3(0.5f, 100.5f, 5.5f), Vec3(1.0f, 0.0f, 0.0f), 10.0f, OneBlock{});
    CHECK(r.hit);
    CHECK(r.block == Vec3i(5, 100, 5));
    CHECK(r.normal == Vec3i(-1, 0, 0));
    CHECK_NEAR(r.dist, 4.5f, 1e-4f);
}

TEST(raycast_hits_top_face_from_above) {
    RaycastHit r = raycast(Vec3(5.5f, 110.5f, 5.5f), Vec3(0.0f, -1.0f, 0.0f), 10.0f, OneBlock{});
    CHECK(r.hit);
    CHECK(r.block == Vec3i(5, 100, 5));
    CHECK(r.normal == Vec3i(0, 1, 0));
    CHECK(r.block + r.normal == Vec3i(5, 101, 5));  // the cell a block would be placed into
}

TEST(raycast_hits_far_face_with_positive_normal) {
    // Ray moves toward -Z and enters the block through its +Z face.
    RaycastHit r = raycast(Vec3(5.5f, 100.5f, 8.0f), Vec3(0.0f, 0.0f, -1.0f), 6.0f, OneBlock{});
    CHECK(r.hit);
    CHECK(r.block == Vec3i(5, 100, 5));
    CHECK(r.normal == Vec3i(0, 0, 1));
    CHECK(r.block + r.normal == Vec3i(5, 100, 6));
    CHECK_NEAR(r.dist, 2.0f, 1e-4f);
}

TEST(raycast_misses_beyond_range) {
    // The block is 4.5 blocks away: a 3-block ray misses, a 6-block (reach) ray hits.
    CHECK(!raycast(Vec3(0.5f, 100.5f, 5.5f), Vec3(1.0f, 0.0f, 0.0f), 3.0f, OneBlock{}).hit);
    CHECK(raycast(Vec3(0.5f, 100.5f, 5.5f), Vec3(1.0f, 0.0f, 0.0f), 6.0f, OneBlock{}).hit);
}

TEST(raycast_misses_when_pointing_away) {
    CHECK(!raycast(Vec3(0.5f, 100.5f, 5.5f), Vec3(-1.0f, 0.0f, 0.0f), 10.0f, OneBlock{}).hit);
}
