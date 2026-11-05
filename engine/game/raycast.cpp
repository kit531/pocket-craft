#include "raycast.h"

#include <limits>

#include "blocks.h"

namespace pc {

RaycastHit raycast(const Vec3& origin, const Vec3& dirIn, float maxDist, const BlockGetter& getBlock) {
    RaycastHit r;
    Vec3 d = normalize(dirIn);
    const float o[3] = {origin.x, origin.y, origin.z};
    const float dv[3] = {d.x, d.y, d.z};
    int cell[3], step[3], normal[3] = {0, 0, 0};
    float tMax[3], tDelta[3];
    const float inf = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        cell[i] = floorToInt(o[i]);
        step[i] = dv[i] > 0 ? 1 : -1;
        tDelta[i] = dv[i] != 0 ? std::fabs(1.0f / dv[i]) : inf;
        float next = dv[i] > 0 ? (cell[i] + 1 - o[i]) : (o[i] - cell[i]);
        tMax[i] = dv[i] != 0 ? next * tDelta[i] : inf;
    }
    float t = 0;
    for (;;) {
        if (blockInfo(getBlock(cell[0], cell[1], cell[2])).solid) {
            r.hit = true;
            r.block = {cell[0], cell[1], cell[2]};
            r.normal = {normal[0], normal[1], normal[2]};
            r.dist = t;
            return r;
        }
        int a = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        t = tMax[a];
        if (t > maxDist) return r;
        cell[a] += step[a];
        tMax[a] += tDelta[a];
        normal[0] = normal[1] = normal[2] = 0;
        normal[a] = -step[a];
    }
}

}  // namespace pc
