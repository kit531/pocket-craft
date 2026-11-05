// World tests against the public API only. Generation and save tests need the real terrain and saving; with the
// stub world they fail (expected). Edits are placed high in the air (y 105) where no terrain exists.
#include <cstdint>

#include "blocks.h"
#include "chunk.h"
#include "pc_math.h"
#include "test.h"
#include "test_util.h"
#include "world.h"

using namespace pc;
using pctest::TempDir;
using pctest::waitLoaded;

TEST(world_generation_is_deterministic_for_seed) {
    TempDir dirA("gen_a"), dirB("gen_b");
    const uint32_t seed = 987654;
    World a(seed, dirA.path());
    World b(seed, dirB.path());
    CHECK(waitLoaded(a, Vec3(0.0f, 64.0f, 0.0f), -32, 31, -32, 31));
    CHECK(waitLoaded(b, Vec3(0.0f, 64.0f, 0.0f), -32, 31, -32, 31));
    int nonAir = 0, mismatches = 0;
    for (int z = -32; z < 32; z += 3)
        for (int x = -32; x < 32; x += 3)
            for (int y = 0; y < CHUNK_H; y += 2) {
                const uint8_t ia = a.getBlock(x, y, z);
                const uint8_t ib = b.getBlock(x, y, z);
                if (ia != AIR) ++nonAir;
                if (ia != ib) ++mismatches;
            }
    CHECK(nonAir > 0);
    CHECK_EQ(mismatches, 0);
}

TEST(world_set_get_round_trip) {
    TempDir dir("roundtrip");
    World w(42, dir.path());
    CHECK(waitLoaded(w, Vec3(200.5f, 105.0f, 200.5f), 200, 200, 200, 200));
    CHECK(w.setBlock(200, 105, 200, STONE));
    CHECK_EQ(w.getBlock(200, 105, 200), STONE);
    CHECK(w.setBlock(200, 105, 200, AIR));
    CHECK_EQ(w.getBlock(200, 105, 200), AIR);

    // Negative coordinates go to the chunk floorDiv(-17, 16) = -2.
    CHECK(waitLoaded(w, Vec3(-16.5f, 105.0f, -2.5f), -17, -17, -3, -3));
    CHECK(w.setBlock(-17, 105, -3, GLASS));
    CHECK_EQ(w.getBlock(-17, 105, -3), GLASS);

    // Out of the 0..127 height range.
    CHECK(!w.setBlock(200, -1, 200, STONE));
    CHECK(!w.setBlock(200, CHUNK_H, 200, STONE));
}

TEST(world_save_and_reload_round_trip) {
    TempDir dir("save");
    const uint32_t seed = 555;
    {
        World w(seed, dir.path());
        CHECK(waitLoaded(w, Vec3(300.5f, 105.0f, 300.5f), 300, 301, 300, 300));
        CHECK(w.setBlock(300, 105, 300, COBBLE));
        CHECK(w.setBlock(301, 106, 300, PLANKS));
        w.saveAll();
    }
    {
        World w(seed, dir.path());
        CHECK(waitLoaded(w, Vec3(300.5f, 105.0f, 300.5f), 300, 301, 300, 300));
        CHECK_EQ(w.getBlock(300, 105, 300), COBBLE);
        CHECK_EQ(w.getBlock(301, 106, 300), PLANKS);
    }
}
