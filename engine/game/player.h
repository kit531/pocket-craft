// Player physics: AABB 0.6 x 1.8, gravity, jump, swimming, flying. Internal to engine/game.
#pragma once
#include "pc_math.h"

namespace pc {

class World;

struct PlayerInput {
    float moveX = 0, moveZ = 0;  // strafe right / forward, -1..1
    bool jump = false, sneak = false;
};

class Player {
public:
    static constexpr float kWidth = 0.6f, kHeight = 1.8f, kEyeHeight = 1.62f;
    static constexpr float kReach = 6.0f;

    Vec3 pos{0.5f, 100.0f, 0.5f};  // feet centre
    Vec3 vel;
    float yaw = 0, pitch = 0;      // yaw 0 looks toward -Z, increasing yaw turns right
    bool flying = false, onGround = false, inWater = false;

    void update(const World& world, const PlayerInput& in, float dt);
    Vec3 eye() const { return {pos.x, pos.y + kEyeHeight, pos.z}; }
    Vec3 forward() const;
    bool intersectsBlock(int x, int y, int z) const;

private:
    bool collides(const World& world, const Vec3& p) const;
    void moveAxis(const World& world, int axis, float delta);
};

}  // namespace pc
