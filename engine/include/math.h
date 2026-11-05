// Tiny math library. Right-handed, Y up. Mat4 is column-major: m[col*4 + row].
#pragma once
#include <cmath>
#include <cstdint>

namespace pc {

constexpr float kPi = 3.14159265358979f;

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

struct Vec3i {
    int x = 0, y = 0, z = 0;
    Vec3i() = default;
    constexpr Vec3i(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
    bool operator==(const Vec3i& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3i& o) const { return !(*this == o); }
    Vec3i operator+(const Vec3i& o) const { return {x + o.x, y + o.y, z + o.z}; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 1e-12f ? v * (1.0f / l) : Vec3{};
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// Floor division / modulo for negative world coordinates (chunk = floorDiv(x, 16)).
inline int floorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
inline int floorMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }
inline int floorToInt(float v) { return static_cast<int>(std::floor(v)); }

struct Mat4 {
    float m[16];

    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    static Mat4 translate(const Vec3& t) {
        Mat4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    // Vulkan clip space: Y points down, depth 0..1. Camera looks down -Z in view space.
    static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
        float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 r{};
        r.m[0] = f / aspect;
        r.m[5] = -f;
        r.m[10] = zFar / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = zNear * zFar / (zNear - zFar);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
};

// a * b (apply b first, then a).
inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
inline Mat4 operator*(const Mat4& a, const Mat4& b) { return multiply(a, b); }

// Returns clip-space xyzw.
inline void transform(const Mat4& a, const Vec3& p, float out[4]) {
    for (int row = 0; row < 4; ++row)
        out[row] = a.m[row] * p.x + a.m[4 + row] * p.y + a.m[8 + row] * p.z + a.m[12 + row];
}

}  // namespace pc
