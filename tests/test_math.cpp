#include <cmath>

#include "pc_math.h"
#include "test.h"

using namespace pc;

namespace {
bool sameMat(const Mat4& a, const Mat4& b, float eps = 1e-5f) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(a.m[i] - b.m[i]) > eps) return false;
    return true;
}
}  // namespace

TEST(math_floor_div_and_mod_negative) {
    CHECK_EQ(floorDiv(-1, 16), -1);
    CHECK_EQ(floorDiv(-16, 16), -1);
    CHECK_EQ(floorDiv(-17, 16), -2);
    CHECK_EQ(floorDiv(-33, 16), -3);
    CHECK_EQ(floorDiv(0, 16), 0);
    CHECK_EQ(floorDiv(15, 16), 0);
    CHECK_EQ(floorDiv(16, 16), 1);
    CHECK_EQ(floorMod(-1, 16), 15);
    CHECK_EQ(floorMod(-16, 16), 0);
    CHECK_EQ(floorMod(-17, 16), 15);
    CHECK_EQ(floorMod(17, 16), 1);
    for (int x = -100; x <= 100; ++x) CHECK_EQ(floorDiv(x, 16) * 16 + floorMod(x, 16), x);
}

TEST(math_matrix_multiply_identity) {
    const Mat4 t = Mat4::translate(Vec3(1.5f, -2.0f, 3.0f));
    const Mat4 p = Mat4::perspective(1.2f, 16.0f / 9.0f, 0.1f, 200.0f);
    const Mat4 v = Mat4::lookAt(Vec3(3.0f, 4.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    CHECK(sameMat(multiply(Mat4::identity(), t), t));
    CHECK(sameMat(multiply(t, Mat4::identity()), t));
    CHECK(sameMat(multiply(Mat4::identity(), p), p));
    CHECK(sameMat(multiply(p, Mat4::identity()), p));
    CHECK(sameMat(multiply(v, Mat4::identity()), v));
    // Translations compose by adding offsets.
    CHECK(sameMat(multiply(Mat4::translate(Vec3(1, 2, 3)), Mat4::translate(Vec3(4, 5, 6))),
                  Mat4::translate(Vec3(5, 7, 9))));
}

TEST(math_perspective_sanity) {
    const float zn = 0.1f, zf = 100.0f;
    const float fov = 70.0f * kPi / 180.0f;
    const Mat4 p = Mat4::perspective(fov, 1.0f, zn, zf);
    float c[4];
    transform(p, Vec3(0.0f, 0.0f, -10.0f), c);  // straight ahead -> screen centre
    CHECK_NEAR(c[0] / c[3], 0.0f, 1e-6f);
    CHECK_NEAR(c[1] / c[3], 0.0f, 1e-6f);
    transform(p, Vec3(0.0f, 1.0f, -10.0f), c);  // above the centre -> negative y (Vulkan: Y down)
    CHECK(c[1] / c[3] < 0.0f);
    transform(p, Vec3(0.0f, 0.0f, -zn), c);  // near plane -> depth 0
    CHECK_NEAR(c[2] / c[3], 0.0f, 1e-5f);
    transform(p, Vec3(0.0f, 0.0f, -zf), c);  // far plane -> depth 1
    CHECK_NEAR(c[2] / c[3], 1.0f, 1e-5f);
    // A point at the top edge of the frustum at distance 10 lands on ndc y = -1.
    const float halfHeight = 10.0f * std::tan(fov * 0.5f);
    transform(p, Vec3(0.0f, halfHeight, -10.0f), c);
    CHECK_NEAR(c[1] / c[3], -1.0f, 1e-4f);
}

TEST(math_look_at_view_space) {
    // Looking down -Z with +Y up: the target is straight ahead (view -Z) and +X is on the right.
    const Mat4 v = Mat4::lookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f));
    float o[4];
    transform(v, Vec3(0.0f, 0.0f, -5.0f), o);
    CHECK_NEAR(o[0], 0.0f, 1e-5f);
    CHECK_NEAR(o[1], 0.0f, 1e-5f);
    CHECK_NEAR(o[2], -5.0f, 1e-5f);
    transform(v, Vec3(1.0f, 0.0f, 0.0f), o);
    CHECK(o[0] > 0.0f);
}
