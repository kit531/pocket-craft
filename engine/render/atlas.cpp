#include "atlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "blocks.h"

namespace pc {
namespace {

struct Rgba { float r, g, b, a; };

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
float rnd(int x, int y, int seed) {
    uint32_t h = hash32(uint32_t(x) * 0x1f123bb5U ^ hash32(uint32_t(y) * 0x9e3779b1U ^ hash32(uint32_t(seed) + 0x85ebca6bU)));
    return float(h & 0xffffU) / 65535.0f;
}
int wrap(int v, int p) { return ((v % p) + p) % p; }

// Tileable value noise: lattice of `period` x `period` cells over the 16 px tile, smooth interpolation.
float vnoise(float x, float y, int period, int seed) {
    float fx = x / 16.0f * period, fy = y / 16.0f * period;
    int ix = int(std::floor(fx)), iy = int(std::floor(fy));
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    float a = rnd(wrap(ix, period), wrap(iy, period), seed), b = rnd(wrap(ix + 1, period), wrap(iy, period), seed);
    float c = rnd(wrap(ix, period), wrap(iy + 1, period), seed), d = rnd(wrap(ix + 1, period), wrap(iy + 1, period), seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

Rgba rgb(float r, float g, float b, float a = 255) { return {r, g, b, a}; }
Rgba scale(Rgba c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Rgba mix(Rgba a, Rgba b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
// Picks one of n palette entries by a 0..1 value: every tile uses only a handful of flat shades (calm, readable at 16 px).
template <size_t N>
Rgba pick(const Rgba (&pal)[N], float v) {
    int i = int(v * float(N));
    return pal[std::clamp(i, 0, int(N) - 1)];
}
// Soft, low amplitude field in 0..1 (mostly mid values) built from smooth noise plus a little per-pixel grain.
float field(int x, int y, int seed, float grain = 0.25f) {
    float n = 0.6f * vnoise(float(x), float(y), 4, seed) + 0.4f * vnoise(float(x), float(y), 8, seed + 1);
    return (1.0f - grain) * n + grain * rnd(x, y, seed + 2);
}

const Rgba kGrass[4] = {rgb(84, 122, 62), rgb(94, 134, 68), rgb(104, 144, 74), rgb(116, 154, 82)};
const Rgba kDirt[4] = {rgb(108, 82, 58), rgb(120, 92, 64), rgb(130, 100, 70), rgb(140, 108, 78)};

Rgba grassTop(int x, int y) { return pick(kGrass, std::clamp((field(x, y, 11) - 0.2f) * 1.6f, 0.0f, 0.999f)); }
Rgba dirt(int x, int y) {
    float v = field(x, y, 21);
    if (rnd(x, y, 23) > 0.94f) return rgb(94, 70, 50);  // a few dark pebbles
    return pick(kDirt, std::clamp((v - 0.2f) * 1.6f, 0.0f, 0.999f));
}
Rgba grassSide(int x, int y) {
    int depth = 3 + int(rnd(x / 2, 0, 31) * 2.99f);  // fringe of 3..5 rows, ragged in 2 px steps
    if (y < depth) {
        Rgba c = grassTop(x, y + 5);
        if (y == depth - 1) c = rgb(74, 108, 54);     // shadowed lower edge of the fringe
        return c;
    }
    if (y == depth) return rgb(100, 76, 54);          // soft shadow just below the grass
    return dirt(x, y);
}
Rgba stone(int x, int y) {
    const Rgba pal[4] = {rgb(118, 120, 124), rgb(126, 128, 131), rgb(134, 136, 139), rgb(142, 144, 146)};
    float v = field(x, y, 41, 0.3f);
    if (rnd(x, y, 44) > 0.95f) return rgb(108, 110, 115);  // sparse darker speckle
    return pick(pal, std::clamp((v - 0.2f) * 1.6f, 0.0f, 0.999f));
}
Rgba cobble(int x, int y) {
    // Cellular pattern on a 4x4 jittered grid, wrapped so the tile repeats.
    float px = x + 0.5f, py = y + 0.5f;
    float f1 = 99, f2 = 99; int id = 0;
    for (int cy = -1; cy <= 4; ++cy)
        for (int cx = -1; cx <= 4; ++cx) {
            int wx = wrap(cx, 4), wy = wrap(cy, 4);
            float jx = (cx * 4 + 0.5f + rnd(wx, wy, 51) * 3.0f), jy = (cy * 4 + 0.5f + rnd(wx, wy, 52) * 3.0f);
            float d = std::sqrt((px - jx) * (px - jx) + (py - jy) * (py - jy));
            if (d < f1) { f2 = f1; f1 = d; id = wy * 4 + wx; }
            else if (d < f2) f2 = d;
        }
    if (f2 - f1 < 0.95f) return rgb(86, 88, 92);  // mortar
    const Rgba pal[3] = {rgb(122, 124, 128), rgb(134, 136, 139), rgb(146, 148, 150)};
    Rgba c = pick(pal, rnd(id, 7, 53));
    if (f2 - f1 < 1.9f) c = scale(c, 0.92f);      // rounded, slightly darker rim
    return c;
}
Rgba sand(int x, int y) {
    const Rgba pal[4] = {rgb(206, 194, 150), rgb(214, 202, 158), rgb(222, 210, 168), rgb(228, 217, 178)};
    if (rnd(x, y, 62) > 0.95f) return rgb(196, 183, 140);
    return pick(pal, std::clamp((field(x, y, 61, 0.4f) - 0.2f) * 1.6f, 0.0f, 0.999f));
}
Rgba water(int x, int y) {
    const float tp = 6.2831853f / 16.0f;
    float w = 0.55f * std::sin((x + y * 2) * tp) + 0.45f * std::sin((x * 2 - y) * tp + 1.3f);  // tiles seamlessly
    const Rgba pal[3] = {rgb(46, 94, 150, 176), rgb(56, 108, 164, 168), rgb(78, 134, 186, 160)};
    return pick(pal, std::clamp(w * 0.35f + 0.5f, 0.0f, 0.999f));
}
Rgba logTop(int x, int y) {
    float dx = std::fabs(x - 7.5f), dy = std::fabs(y - 7.5f);
    float d = std::max(dx, dy);
    if (d > 6.6f) return rgb(96, 72, 50);  // bark rim
    int ring = int(d);
    if (ring == 0) return rgb(138, 104, 70);
    return (ring % 2 == 0) ? rgb(176, 144, 100) : rgb(160, 128, 88);
}
Rgba logSide(int x, int y) {
    const Rgba pal[3] = {rgb(92, 70, 48), rgb(104, 80, 56), rgb(116, 90, 64)};
    float col = rnd(x, 0, 91);
    if (x % 4 == 1) return rgb(72, 54, 38);            // vertical bark grooves
    Rgba c = pick(pal, col);
    if (rnd(x, y / 4, 92) > 0.85f) c = pal[0];         // short darker streaks
    return c;
}
Rgba leaves(int x, int y) {
    const Rgba pal[3] = {rgb(48, 92, 46), rgb(60, 110, 54), rgb(74, 126, 62)};
    if (rnd(x, y, 101) < 0.10f) return {0, 0, 0, 0};  // see-through holes
    float v = 0.7f * vnoise(float(x), float(y), 4, 102) + 0.3f * rnd(x, y, 103);
    Rgba c = pick(pal, std::clamp((v - 0.15f) * 1.5f, 0.0f, 0.999f));
    if (rnd(x + 1, y, 101) < 0.10f || rnd(x, y + 1, 101) < 0.10f) c = pal[0];  // darker next to holes: reads as depth
    return c;
}
Rgba planks(int x, int y) {
    int b = y / 4, r = y % 4;
    const Rgba pal[3] = {rgb(168, 134, 90), rgb(176, 142, 96), rgb(184, 150, 104)};
    if (r == 3) return rgb(126, 96, 62);               // seam between boards
    int joint = (b % 2 == 0) ? 5 : 12;
    if (x == joint) return rgb(134, 102, 66);
    Rgba c = pick(pal, rnd(b, 0, 111));
    if (r == 0) c = scale(c, 1.05f);
    if (rnd(x / 3, y, 113) > 0.92f) c = scale(c, 0.93f);  // faint grain
    return c;
}
Rgba glass(int x, int y) {
    int e = std::min(std::min(x, 15 - x), std::min(y, 15 - y));
    if (e == 0) return rgb(206, 226, 232, 225);        // thin light frame
    if (e == 1) return rgb(196, 220, 230, 50);
    int d = (x + y) % 16;
    if (x > 2 && x < 13 && y > 2 && y < 13 && (d == 7 || d == 8) && ((x + y) % 5 != 0)) return rgb(255, 255, 255, 70);  // faint glint
    return rgb(206, 228, 236, 22);
}
Rgba bedrock(int x, int y) {
    const Rgba pal[4] = {rgb(34, 34, 38), rgb(46, 46, 51), rgb(60, 60, 65), rgb(74, 74, 79)};
    float v = 0.65f * rnd(x / 2, y / 2, 121) + 0.35f * rnd(x, y, 122);
    return pick(pal, v);
}
Rgba missing(int x, int y) { return ((x / 4 + y / 4) & 1) ? rgb(255, 0, 255) : rgb(20, 20, 20); }

Rgba paint(int tile, int x, int y) {
    switch (tile) {
        case TILE_GRASS_TOP: return grassTop(x, y);
        case TILE_GRASS_SIDE: return grassSide(x, y);
        case TILE_DIRT: return dirt(x, y);
        case TILE_STONE: return stone(x, y);
        case TILE_SAND: return sand(x, y);
        case TILE_WATER: return water(x, y);
        case TILE_LOG_TOP: return logTop(x, y);
        case TILE_LOG_SIDE: return logSide(x, y);
        case TILE_LEAVES: return leaves(x, y);
        case TILE_PLANKS: return planks(x, y);
        case TILE_GLASS: return glass(x, y);
        case TILE_COBBLE: return cobble(x, y);
        case TILE_BEDROCK: return bedrock(x, y);
        default: return missing(x, y);
    }
}

uint8_t q(float v) { return uint8_t(std::clamp(v + 0.5f, 0.0f, 255.0f)); }

}  // namespace

size_t atlasMipOffset(int level) {
    size_t o = 0;
    for (int i = 0; i < level; ++i) o += atlasMipBytes(i);
    return o;
}
size_t atlasTotalBytes() { return atlasMipOffset(kAtlasMipLevels); }

void buildAtlas(std::vector<uint8_t>& out) {
    out.assign(atlasTotalBytes(), 0);
    uint8_t* l0 = out.data();
    for (int ty = 0; ty < 16; ++ty)
        for (int tx = 0; tx < 16; ++tx)
            for (int y = 0; y < kAtlasTilePx; ++y)
                for (int x = 0; x < kAtlasTilePx; ++x) {
                    Rgba c = paint(ty * 16 + tx, x, y);
                    uint8_t* p = l0 + (size_t(ty * kAtlasTilePx + y) * kAtlasSize + tx * kAtlasTilePx + x) * 4;
                    p[0] = q(c.r); p[1] = q(c.g); p[2] = q(c.b); p[3] = q(c.a);
                }
    for (int lv = 1; lv < kAtlasMipLevels; ++lv) {
        const uint8_t* src = out.data() + atlasMipOffset(lv - 1);
        uint8_t* dst = out.data() + atlasMipOffset(lv);
        int sw = atlasMipSize(lv - 1), dw = atlasMipSize(lv);
        for (int y = 0; y < dw; ++y)
            for (int x = 0; x < dw; ++x) {
                float r = 0, g = 0, b = 0, a = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const uint8_t* s = src + (size_t(y * 2 + j) * sw + x * 2 + i) * 4;
                        r += s[0] * s[3]; g += s[1] * s[3]; b += s[2] * s[3]; a += s[3];
                    }
                uint8_t* d = dst + (size_t(y) * dw + x) * 4;
                if (a > 0) { d[0] = q(r / a); d[1] = q(g / a); d[2] = q(b / a); }
                d[3] = q(a / 4);
            }
    }
}

}  // namespace pc
