// Internal: deterministic terrain generation.
#pragma once
#include <cstdint>

#include "column.h"

namespace pc {

constexpr int kSeaLevel = 40;

// Fills c.blocks completely (terrain, caves, trees, including trees of neighbouring columns that reach into it).
// Pure function of (seed, cx, cz). Does not compute light/counters (call finalizeColumn).
void generateColumn(uint32_t seed, int cx, int cz, Column& c);
// Surface height (y of the topmost ground block, before caves/trees) at world x,z.
int terrainHeight(uint32_t seed, int x, int z);

}  // namespace pc
