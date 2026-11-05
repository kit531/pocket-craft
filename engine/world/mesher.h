// Internal: section meshing from an immutable snapshot (so workers never read live column data).
#pragma once
#include <cstdint>
#include <memory>

#include "chunk.h"

namespace pc {

constexpr int kSnap = 18;  // 16 + 1 border cell on each side
struct SectionSnapshot {
    uint8_t blocks[kSnap * kSnap * kSnap];
    uint8_t sky[kSnap * kSnap * kSnap];  // sky light 0..15 per cell
    static int idx(int x, int y, int z) { return ((y + 1) * kSnap + (z + 1)) * kSnap + (x + 1); }  // x,y,z in -1..16
};

// Returns nullptr when the section produces no geometry.
//
// Winding: every quad is clockwise as seen from outside the block in a Y-up view (corner 0 = top-left,
// 1 = top-right, 2 = bottom-right, 3 = bottom-left; top/bottom faces use north (-Z) / +Z as "up"),
// i.e. VK_FRONT_FACE_CLOCKWISE after the Y-flipped projection. Indices (0,1,2, 0,2,3) or the flipped
// diagonal (1,2,3, 1,3,0) chosen by ambient occlusion.
std::shared_ptr<SectionMesh> meshSection(const SectionSnapshot& snap, SectionKey key);

}  // namespace pc
