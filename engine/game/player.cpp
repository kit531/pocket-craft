#include "player.h"

#include "blocks.h"
#include "world.h"

namespace pc {
namespace {
constexpr float kHalf = Player::kWidth * 0.5f;
constexpr float kEps = 1e-4f;
constexpr float kGravity = 25.0f, kJumpSpeed = 8.4f, kMaxFall = 50.0f;
constexpr float kWalkSpeed = 4.3f, kFlySpeed = 11.0f, kSwimSpeed = 2.6f;
}  // namespace

Vec3 Player::forward() const {
    float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

bool Player::intersectsBlock(int x, int y, int z) const {
    return pos.x + kHalf > x && pos.x - kHalf < x + 1 && pos.y + kHeight > y && pos.y < y + 1 &&
           pos.z + kHalf > z && pos.z - kHalf < z + 1;
}

bool Player::collides(const World& w, const Vec3& p) const {
    int x0 = floorToInt(p.x - kHalf + kEps), x1 = floorToInt(p.x + kHalf - kEps);
    int y0 = floorToInt(p.y + kEps), y1 = floorToInt(p.y + kHeight - kEps);
    int z0 = floorToInt(p.z - kHalf + kEps), z1 = floorToInt(p.z + kHalf - kEps);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (blockInfo(w.getBlock(x, y, z)).solid) return true;
    return false;
}

// Per-axis move; on contact snaps flush to the block face and zeroes that velocity component.
void Player::moveAxis(const World& w, int axis, float delta) {
    if (delta == 0) return;
    float* c = axis == 0 ? &pos.x : (axis == 1 ? &pos.y : &pos.z);
    float* v = axis == 0 ? &vel.x : (axis == 1 ? &vel.y : &vel.z);
    float old = *c;
    *c += delta;
    if (!collides(w, pos)) return;
    float ext = axis == 1 ? (delta > 0 ? kHeight : 0.0f) : (delta > 0 ? kHalf : -kHalf);
    if (delta > 0) *c = std::floor(old + ext + delta) - ext;
    else *c = std::floor(old + ext + delta) + 1 - ext;
    if (collides(w, pos)) *c = old;  // snapping failed (corner case): stay put
    if (axis == 1 && delta < 0) onGround = true;
    *v = 0;
}

void Player::update(const World& w, const PlayerInput& in, float dt) {
    int fx = floorToInt(pos.x), fz = floorToInt(pos.z);
    inWater = blockInfo(w.getBlock(fx, floorToInt(pos.y + 0.4f), fz)).liquid;

    float speed = flying ? kFlySpeed : (inWater ? kSwimSpeed : kWalkSpeed);
    float sy = std::sin(yaw), cy = std::cos(yaw);
    float len = std::sqrt(in.moveX * in.moveX + in.moveZ * in.moveZ);
    float k = len > 1.0f ? 1.0f / len : 1.0f;
    float wx = (sy * in.moveZ + cy * in.moveX) * k * speed;
    float wz = (-cy * in.moveZ + sy * in.moveX) * k * speed;
    float blend = (onGround || flying || inWater) ? 1.0f : 0.15f;  // limited air control
    vel.x += (wx - vel.x) * blend;
    vel.z += (wz - vel.z) * blend;

    if (flying) {
        vel.y = ((in.jump ? 1.0f : 0.0f) - (in.sneak ? 1.0f : 0.0f)) * kFlySpeed * 0.7f;
    } else if (inWater) {
        vel.y += -6.0f * dt;
        vel.y *= 0.9f;
        if (in.jump) vel.y = 3.5f;
    } else {
        vel.y -= kGravity * dt;
        if (vel.y < -kMaxFall) vel.y = -kMaxFall;
        if (in.jump && onGround) vel.y = kJumpSpeed;
    }

    onGround = false;
    moveAxis(w, 0, vel.x * dt);
    moveAxis(w, 2, vel.z * dt);
    moveAxis(w, 1, vel.y * dt);
}

}  // namespace pc
