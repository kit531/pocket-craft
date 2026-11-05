// Internal: one 16x16x128 chunk column (blocks + nibble sky light + per-section bookkeeping).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "blocks.h"
#include "chunk.h"

namespace pc {

constexpr int kColumnCells = CHUNK_W * CHUNK_W * CHUNK_H;  // 32768

// Shared with mesh jobs so stale results can be recognised without touching the column.
struct ColShared {
    std::atomic<uint32_t> ver[SECTIONS_PER_COLUMN];  // bumped on every change that affects a section's mesh
    std::atomic<uint32_t> sent{0};                   // bit sy: a non-empty mesh for sy is currently on the renderer
    ColShared() { for (auto& v : ver) v.store(1); }
};

struct Column {
    ChunkKey key;
    uint8_t blocks[kColumnCells];       // index (y<<8)|(z<<4)|x : each section is one contiguous 4 KB slab
    uint8_t sky[kColumnCells / 2];      // 4-bit sky light, two cells per byte
    uint16_t nonAir[SECTIONS_PER_COLUMN];  // count of non-air blocks per section (0 => skip meshing)
    uint8_t top[CHUNK_W * CHUNK_W];     // (highest non-air y)+1, 0 = empty column; index (z<<4)|x
    bool modified = false;              // differs from what generation would produce / what is on disk
    uint8_t dirty = 0;                  // bit sy: section needs (re)meshing; game thread only
    std::shared_ptr<ColShared> cs = std::make_shared<ColShared>();

    static int idx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
    uint8_t get(int x, int y, int z) const { return blocks[idx(x, y, z)]; }
    uint8_t skyAt(int i) const { return (sky[i >> 1] >> ((i & 1) * 4)) & 15; }
    void setSky(int i, int v) {
        uint8_t& b = sky[i >> 1];
        b = static_cast<uint8_t>((i & 1) ? ((b & 0x0F) | (v << 4)) : ((b & 0xF0) | v));
    }
};

inline bool blockOpaque(uint8_t id) { const BlockInfo& b = blockInfo(id); return !b.transparent; }
// How much sky light a non-opaque block absorbs when light passes down through it.
inline int skyAbsorb(uint8_t id) { return id == WATER ? 2 : (id == LEAVES ? 1 : 0); }

void finalizeColumn(Column& c);        // recompute nonAir counts, top[] and light from blocks[]
void computeSkyLight(Column& c);       // light only

}  // namespace pc
