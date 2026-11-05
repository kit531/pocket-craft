#include "terrain.h"

#include <cstring>

#include "noise.h"

namespace pc {
namespace {

constexpr uint32_t kSaltCont = 0x1001, kSaltRug = 0x2002, kSaltDet = 0x3003, kSaltForest = 0x4004,
                   kSaltTree = 0x5005, kSaltCave1 = 0x6006, kSaltCave2 = 0x7007, kSaltBed = 0x8008;

float continent(uint32_t s, int x, int z) { return fbm2(x * 0.0035f, z * 0.0035f, 3, s + kSaltCont); }
float ruggedness(uint32_t s, int x, int z) { return fbm2(x * 0.005f + 40.f, z * 0.005f - 70.f, 2, s + kSaltRug); }
float forestNoise(uint32_t s, int x, int z) { return fbm2(x * 0.012f, z * 0.012f, 2, s + kSaltForest); }

// Treeline limits.
constexpr int kMaxTreeGround = 86;

}  // namespace

int terrainHeight(uint32_t s, int x, int z) {
    float cont = continent(s, x, z);          // ~0.3..0.7
    float rug = ruggedness(s, x, z);
    float det = fbm2(x * 0.025f, z * 0.025f, 4, s + kSaltDet);
    float hill = smoothstepf(0.38f, 0.58f, rug);   // hills
    float mount = smoothstepf(0.52f, 0.66f, rug) * smoothstepf(0.40f, 0.50f, cont);  // mountains, inland only
    float base = 48.0f + (cont - 0.5f) * 56.0f;
    float amp = 7.0f + 26.0f * hill + 60.0f * mount;
    float h = base + (det - 0.5f) * amp * 2.0f + mount * 14.0f;
    int hi = static_cast<int>(h);
    return hi < 3 ? 3 : (hi > 120 ? 120 : hi);
}

namespace {

inline void putIfAir(Column& c, int cx, int cz, int wx, int y, int wz, uint8_t id) {
    int lx = wx - cx * CHUNK_W, lz = wz - cz * CHUNK_W;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 0 || y >= CHUNK_H) return;
    uint8_t& b = c.blocks[Column::idx(lx, y, lz)];
    if (b == AIR) b = id;  // leaves never overwrite anything; logs are written after with putLog
}
inline void putLog(Column& c, int cx, int cz, int wx, int y, int wz) {
    int lx = wx - cx * CHUNK_W, lz = wz - cz * CHUNK_W;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || y < 0 || y >= CHUNK_H) return;
    uint8_t& b = c.blocks[Column::idx(lx, y, lz)];
    if (b == AIR || b == LEAVES) b = LOG;
}

// Trees of every world column within 2 blocks of this chunk are evaluated here and only the blocks that fall
// inside this chunk are written, so a tree is identical no matter which chunk loads first.
void placeTrees(uint32_t s, int cx, int cz, Column& c) {
    for (int wz = cz * CHUNK_W - 2; wz < cz * CHUNK_W + CHUNK_W + 2; ++wz)
        for (int wx = cx * CHUNK_W - 2; wx < cx * CHUNK_W + CHUNK_W + 2; ++wx) {
            uint32_t h = hash3(wx, 0, wz, s + kSaltTree);
            float roll = hashUnit(h);
            if (roll > 0.05f) continue;  // cheap reject, max density
            float f = forestNoise(s, wx, wz);
            float chance = 0.004f + 0.045f * smoothstepf(0.55f, 0.72f, f);
            if (roll > chance) continue;
            int g = terrainHeight(s, wx, wz);
            if (g <= kSeaLevel + 2 || g > kMaxTreeGround) continue;
            int th = 4 + static_cast<int>((h >> 4) & 3);  // trunk 4..7
            if (g + th + 2 >= CHUNK_H) continue;
            uint32_t hh = hashMix(h ^ 0xABCDEF01u);
            for (int dy = th - 2; dy <= th + 1; ++dy) {
                int r = dy <= th - 1 ? 2 : 1;
                for (int dz = -r; dz <= r; ++dz)
                    for (int dx = -r; dx <= r; ++dx) {
                        bool corner = (dx == r || dx == -r) && (dz == r || dz == -r);
                        if (corner && (r == 2 ? ((hh >> (((dx + 2) * 5 + dz + 2 + dy) & 31)) & 1) : dy == th + 1)) continue;
                        putIfAir(c, cx, cz, wx + dx, g + dy, wz + dz, LEAVES);
                    }
            }
            for (int dy = 1; dy <= th; ++dy) putLog(c, cx, cz, wx, g + dy, wz);
        }
}

}  // namespace

void generateColumn(uint32_t s, int cx, int cz, Column& c) {
    c.key = {cx, cz};
    std::memset(c.blocks, AIR, sizeof(c.blocks));
    int heights[256];
    int maxH = 0;
    for (int z = 0; z < CHUNK_W; ++z)
        for (int x = 0; x < CHUNK_W; ++x) {
            int wx = cx * CHUNK_W + x, wz = cz * CHUNK_W + z;
            int h = terrainHeight(s, wx, wz);
            heights[(z << 4) | x] = h;
            if (h > maxH) maxH = h;
            bool sandy = h <= kSeaLevel + 2;
            bool underwater = h < kSeaLevel;
            uint8_t topBlock = sandy ? (underwater && h < kSeaLevel - 5 ? DIRT : SAND) : (h > 92 ? STONE : GRASS);
            uint8_t under = sandy ? (underwater && h < kSeaLevel - 5 ? DIRT : SAND) : (h > 92 ? STONE : DIRT);
            for (int y = 0; y <= h; ++y) {
                uint8_t b;
                if (y == 0) b = BEDROCK;
                else if (y == 1) b = (hash3(wx, 1, wz, s + kSaltBed) & 1) ? BEDROCK : STONE;
                else if (y == h) b = topBlock;
                else if (y >= h - 3) b = under;
                else b = STONE;
                c.blocks[Column::idx(x, y, z)] = b;
            }
            for (int y = h + 1; y <= kSeaLevel; ++y) c.blocks[Column::idx(x, y, z)] = WATER;
        }

    // Caves: two noise fields sampled on a coarse 4-block lattice (global, so seams match), tunnels where both
    // are near their midline; trilinear interpolation inside each cell.
    {
        const int gy = 17;  // y = 0..64 step 4
        float f[5][17][5];  // [ix][iy][iz]
        for (int ix = 0; ix < 5; ++ix)
            for (int iz = 0; iz < 5; ++iz)
                for (int iy = 0; iy < gy; ++iy) {
                    float wx = static_cast<float>(cx * CHUNK_W + ix * 4), wz = static_cast<float>(cz * CHUNK_W + iz * 4);
                    float wy = static_cast<float>(iy * 4);
                    float a = valueNoise3(wx * 0.035f, wy * 0.05f, wz * 0.035f, s + kSaltCave1) - 0.5f;
                    float b = valueNoise3(wx * 0.035f + 31.f, wy * 0.05f, wz * 0.035f + 7.f, s + kSaltCave2) - 0.5f;
                    f[ix][iy][iz] = a * a + b * b;
                }
        for (int z = 0; z < CHUNK_W; ++z)
            for (int x = 0; x < CHUNK_W; ++x) {
                int lim = heights[(z << 4) | x] - 4;
                if (lim > 63) lim = 63;
                float tx = (x & 3) / 4.0f, tz = (z & 3) / 4.0f;
                int ix = x >> 2, iz = z >> 2;
                for (int y = 3; y <= lim; ++y) {
                    int iy = y >> 2;
                    float ty = (y & 3) / 4.0f;
                    auto at = [&](int a, int b, int d) { return f[ix + a][iy + b][iz + d]; };
                    float x00 = lerpf(at(0, 0, 0), at(1, 0, 0), tx), x10 = lerpf(at(0, 1, 0), at(1, 1, 0), tx);
                    float x01 = lerpf(at(0, 0, 1), at(1, 0, 1), tx), x11 = lerpf(at(0, 1, 1), at(1, 1, 1), tx);
                    float v = lerpf(lerpf(x00, x10, ty), lerpf(x01, x11, ty), tz);
                    if (v < 0.0030f) c.blocks[Column::idx(x, y, z)] = AIR;
                }
            }
    }
    placeTrees(s, cx, cz, c);
}

}  // namespace pc
