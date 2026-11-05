// Procedural texture atlas (platform independent, no Vulkan). 256x256 RGBA8, 16x16 tiles of 16 px, tile index = row*16+col.
// All mip levels (256, 128, 64, 32, 16) are packed back to back, level 0 first. Mips stop at 16x16 so that tiles never bleed
// into each other (at the last level each tile is exactly one texel). Colour in mips is alpha weighted (no dark leaf fringes).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pc {

constexpr int kAtlasSize = 256;
constexpr int kAtlasTilePx = 16;
constexpr int kAtlasMipLevels = 5;

inline int atlasMipSize(int level) { return kAtlasSize >> level; }
inline size_t atlasMipBytes(int level) { return size_t(atlasMipSize(level)) * atlasMipSize(level) * 4; }
size_t atlasMipOffset(int level);   // byte offset of a level inside the packed buffer
size_t atlasTotalBytes();

// Deterministic: the same bytes on every platform. `out` is resized to atlasTotalBytes().
void buildAtlas(std::vector<uint8_t>& out);

}  // namespace pc
