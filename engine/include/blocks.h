// Block ids and static per-block properties. Ids are uint8 and stored in saves: never reorder, only append.
#pragma once
#include <cstdint>

namespace pc {

enum BlockId : uint8_t {
    AIR = 0, GRASS, DIRT, STONE, SAND, WATER, LOG, LEAVES, PLANKS, GLASS, COBBLE, BEDROCK,
    BLOCK_COUNT
};

// Atlas tile indices (16x16 tiles in a 256x256 atlas; index = row*16 + col). The renderer paints them.
enum Tile : uint8_t {
    TILE_GRASS_TOP = 0, TILE_GRASS_SIDE, TILE_DIRT, TILE_STONE, TILE_SAND, TILE_WATER,
    TILE_LOG_TOP, TILE_LOG_SIDE, TILE_LEAVES, TILE_PLANKS, TILE_GLASS, TILE_COBBLE, TILE_BEDROCK,
    TILE_COUNT
};

// Face order used everywhere (meshing, MeshVertex::face, tileForFace).
enum Face : uint8_t { FACE_PX = 0, FACE_NX, FACE_PY, FACE_NY, FACE_PZ, FACE_NZ };

struct BlockInfo {
    const char* name;
    bool solid;        // collides with the player and can be targeted by the raycast
    bool transparent;  // does not hide neighbouring faces (air, water, glass, leaves)
    bool liquid;
    uint8_t tileTop, tileBottom, tileSide;
};

inline constexpr BlockInfo kBlockInfo[BLOCK_COUNT] = {
    {"air",     false, true,  false, TILE_GRASS_TOP, TILE_GRASS_TOP, TILE_GRASS_TOP},
    {"grass",   true,  false, false, TILE_GRASS_TOP, TILE_DIRT,      TILE_GRASS_SIDE},
    {"dirt",    true,  false, false, TILE_DIRT,      TILE_DIRT,      TILE_DIRT},
    {"stone",   true,  false, false, TILE_STONE,     TILE_STONE,     TILE_STONE},
    {"sand",    true,  false, false, TILE_SAND,      TILE_SAND,      TILE_SAND},
    {"water",   false, true,  true,  TILE_WATER,     TILE_WATER,     TILE_WATER},
    {"log",     true,  false, false, TILE_LOG_TOP,   TILE_LOG_TOP,   TILE_LOG_SIDE},
    {"leaves",  true,  true,  false, TILE_LEAVES,    TILE_LEAVES,    TILE_LEAVES},
    {"planks",  true,  false, false, TILE_PLANKS,    TILE_PLANKS,    TILE_PLANKS},
    {"glass",   true,  true,  false, TILE_GLASS,     TILE_GLASS,     TILE_GLASS},
    {"cobble",  true,  false, false, TILE_COBBLE,    TILE_COBBLE,    TILE_COBBLE},
    {"bedrock", true,  false, false, TILE_BEDROCK,   TILE_BEDROCK,   TILE_BEDROCK},
};

inline const BlockInfo& blockInfo(uint8_t id) { return kBlockInfo[id < BLOCK_COUNT ? id : 0]; }

inline uint8_t tileForFace(uint8_t id, int face) {
    const BlockInfo& b = blockInfo(id);
    return face == FACE_PY ? b.tileTop : (face == FACE_NY ? b.tileBottom : b.tileSide);
}

// Block placed by each hotbar slot (the Kotlin UI shows 9 slots in this order).
inline constexpr uint8_t kHotbarBlocks[9] = {GRASS, DIRT, STONE, COBBLE, PLANKS, LOG, SAND, GLASS, LEAVES};

}  // namespace pc
