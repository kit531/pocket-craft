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

Rgba grassTop(int x, int y) {
    float n = 0.55f * vnoise(float(x), float(y), 4, 11) + 0.45f * rnd(x, y, 12);
    Rgba c = scale(rgb(84, 152, 48), 0.72f + 0.55f * n);
    float s = rnd(x, y, 13);
    if (s > 0.93f) c = mix(c, rgb(150, 196, 70), 0.6f);
    else if (s < 0.06f) c = scale(c, 0.72f);
    return c;
}
Rgba dirt(int x, int y) {
    float n = 0.5f * vnoise(float(x), float(y), 4, 21) + 0.5f * rnd(x, y, 22);
    Rgba c = scale(rgb(121, 85, 57), 0.78f + 0.4f * n);
    float s = rnd(x, y, 23);
    if (s < 0.10f) c = mix(c, rgb(84, 56, 38), 0.7f);
    else if (s > 0.94f) c = mix(c, rgb(160, 120, 86), 0.7f);
    return c;
}
Rgba grassSide(int x, int y) {
    int depth = 3 + int(rnd(x, 0, 31) * 3.0f);  // ragged fringe 3..5 rows
    if (y < depth) {
        Rgba c = grassTop(x, y + 5);
        if (y == depth - 1) c = scale(c, 0.82f);
        return c;
    }
    return dirt(x, y);
}
Rgba stone(int x, int y) {
    float n = 0.45f * vnoise(float(x), float(y), 4, 41) + 0.25f * vnoise(float(x), float(y), 8, 42) + 0.3f * rnd(x, y, 43);
    Rgba c = scale(rgb(127, 129, 135), 0.74f + 0.5f * n);
    // a few hair cracks
    if (rnd(x / 2, y, 44) > 0.93f && rnd(x, y / 3, 45) > 0.55f) c = scale(c, 0.78f);
    return c;
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
    if (f2 - f1 < 0.95f) return rgb(72, 72, 78);  // mortar
    float tone = 0.72f + 0.38f * rnd(id, 7, 53);
    Rgba c = scale(rgb(132, 132, 136), tone * (1.08f - 0.08f * f1));
    c = scale(c, 0.93f + 0.14f * rnd(x, y, 54));
    return c;
}
Rgba sand(int x, int y) {
    Rgba c = scale(rgb(222, 208, 152), 0.9f + 0.14f * rnd(x, y, 61));
    float s = rnd(x, y, 62);
    if (s < 0.07f) c = mix(c, rgb(190, 170, 118), 0.7f);
    else if (s > 0.95f) c = mix(c, rgb(246, 236, 190), 0.7f);
    return c;
}
Rgba water(int x, int y) {
    const float tp = 6.2831853f / 16.0f;
    float w = 0.5f * std::sin((x * 1 + y * 2) * tp) + 0.35f * std::sin((x * 2 - y * 1) * tp + 1.3f) + 0.15f * (rnd(x, y, 71) - 0.5f) * 2;
    Rgba c = mix(rgb(34, 86, 196), rgb(96, 156, 235), std::clamp(w * 0.5f + 0.35f, 0.0f, 1.0f));
    c.a = 168 + 26 * w;
    return c;
}
Rgba logTop(int x, int y) {
    float dx = std::fabs(x - 7.5f), dy = std::fabs(y - 7.5f);
    float d = std::max(dx, dy);
    if (d > 6.6f) return scale(rgb(98, 72, 44), 0.85f + 0.3f * rnd(x, y, 81));
    int ring = int(d);
    Rgba c = (ring % 2 == 0) ? rgb(190, 152, 96) : rgb(165, 126, 76);
    if (ring == 0) c = rgb(142, 104, 60);
    return scale(c, 0.93f + 0.14f * rnd(x, y, 82));
}
Rgba logSide(int x, int y) {
    float col = 0.78f + 0.3f * rnd(x, 0, 91);
    float streak = 0.92f + 0.16f * rnd(x, y / 5, 92);
    Rgba c = scale(rgb(104, 78, 48), col * streak * (0.94f + 0.12f * rnd(x, y, 93)));
    if (rnd(x / 2, y / 3, 94) > 0.93f) c = scale(c, 0.68f);  // knots
    return c;
}
Rgba leaves(int x, int y) {
    float h = rnd(x, y, 101);
    if (h < 0.20f) return {0, 0, 0, 0};  // see-through holes
    float v = rnd(x, y, 102);
    Rgba c = scale(rgb(62, 132, 42), 0.62f + 0.6f * v);
    if (rnd(x, y, 103) > 0.9f) c = mix(c, rgb(120, 186, 70), 0.55f);
    // darken texels next to a hole so clumps read as depth
    if (rnd(x + 1, y, 101) < 0.20f || rnd(x, y + 1, 101) < 0.20f) c = scale(c, 0.78f);
    return c;
}
Rgba planks(int x, int y) {
    int b = y / 4, r = y % 4;
    float tone = 0.9f + 0.2f * rnd(b, 0, 111);
    Rgba c = scale(rgb(178, 138, 84), tone);
    c = scale(c, 0.93f + 0.1f * rnd(x / 4, b, 112));          // grain streaks along the board
    if (rnd(x, y, 113) > 0.85f) c = scale(c, 0.93f);
    if (r == 3) return rgb(104, 74, 42);                      // seam between boards
    int joint = (b % 2 == 0) ? 5 : 12;
    if (x == joint) return rgb(112, 80, 46);
    if (r == 0) c = scale(c, 1.06f);
    return c;
}
Rgba glass(int x, int y) {
    int e = std::min(std::min(x, 15 - x), std::min(y, 15 - y));
    if (e == 0) return rgb(214, 238, 248, 225);               // frame
    if (e == 1) return rgb(190, 224, 240, 70);
    int d = (x + y) % 16;
    if (x > 2 && x < 13 && y > 2 && y < 13 && (d == 5 || d == 6)) return rgb(255, 255, 255, 120);  // glint
    return rgb(200, 232, 245, 26);
}
Rgba bedrock(int x, int y) {
    float v = 0.6f * rnd(x / 2, y / 2, 121) + 0.4f * rnd(x, y, 122);
    if (v < 0.3f) return rgb(26, 26, 30);
    if (v < 0.62f) return scale(rgb(58, 58, 64), 0.9f + 0.2f * rnd(x, y, 123));
    return scale(rgb(94, 94, 102), 0.9f + 0.2f * rnd(x, y, 124));
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
