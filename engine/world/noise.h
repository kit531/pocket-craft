// Deterministic integer-hash value noise (original implementation). No state, thread-safe.
#pragma once
#include <cmath>
#include <cstdint>

namespace pc {

inline uint32_t hashMix(uint32_t h) {
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
inline uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t s) {
    uint32_t h = hashMix(s ^ 0x9E3779B9u);
    h = hashMix(h ^ static_cast<uint32_t>(x) * 0x27D4EB2Fu);
    h = hashMix(h ^ static_cast<uint32_t>(y) * 0x165667B1u);
    h = hashMix(h ^ static_cast<uint32_t>(z) * 0xD3A2646Cu);
    return h;
}
inline float hashUnit(uint32_t h) { return static_cast<float>(h >> 8) * (1.0f / 16777216.0f); }
inline float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) {
    float t = (x - a) / (b - a);
    return smooth01(t < 0 ? 0 : (t > 1 ? 1 : t));
}

inline float valueNoise2(float x, float z, uint32_t seed) {
    float fx = std::floor(x), fz = std::floor(z);
    int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    float tx = smooth01(x - fx), tz = smooth01(z - fz);
    float a = hashUnit(hash3(ix, 0, iz, seed)), b = hashUnit(hash3(ix + 1, 0, iz, seed));
    float c = hashUnit(hash3(ix, 0, iz + 1, seed)), d = hashUnit(hash3(ix + 1, 0, iz + 1, seed));
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), tz);
}

inline float valueNoise3(float x, float y, float z, uint32_t seed) {
    float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    float tx = smooth01(x - fx), ty = smooth01(y - fy), tz = smooth01(z - fz);
    auto h = [&](int dx, int dy, int dz) { return hashUnit(hash3(ix + dx, iy + dy, iz + dz, seed)); };
    float x00 = lerpf(h(0, 0, 0), h(1, 0, 0), tx), x10 = lerpf(h(0, 1, 0), h(1, 1, 0), tx);
    float x01 = lerpf(h(0, 0, 1), h(1, 0, 1), tx), x11 = lerpf(h(0, 1, 1), h(1, 1, 1), tx);
    return lerpf(lerpf(x00, x10, ty), lerpf(x01, x11, ty), tz);
}

// Fractal sum normalised to roughly 0..1.
inline float fbm2(float x, float z, int octaves, uint32_t seed) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise2(x, z, seed + static_cast<uint32_t>(i) * 101u);
        norm += amp;
        amp *= 0.5f; x *= 2.03f; z *= 2.03f;
        x += 17.3f; z += 9.1f;
    }
    return sum / norm;
}

}  // namespace pc
