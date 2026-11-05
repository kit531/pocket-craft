// Voxel DDA raycast (Amanatides-Woo). Pure function: works with any block source, unit-testable on the host.
#pragma once
#include <cstdint>
#include <functional>

#include "pc_math.h"

namespace pc {

struct RaycastHit {
    bool hit = false;
    Vec3i block;   // the solid block that was hit
    Vec3i normal;  // face normal pointing out of the hit block; block + normal = the cell to place into. 0,0,0 if the ray started inside.
    float dist = 0;
};

using BlockGetter = std::function<uint8_t(int x, int y, int z)>;

// Hits the first block with blockInfo().solid (liquids are skipped). dir need not be normalized.
RaycastHit raycast(const Vec3& origin, const Vec3& dir, float maxDist, const BlockGetter& getBlock);

}  // namespace pc
