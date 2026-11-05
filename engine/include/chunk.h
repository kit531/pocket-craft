// World geometry constants, keys and the mesh data handed from the world to the renderer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace pc {

constexpr int CHUNK_W = 16;               // columns are 16 x 16 blocks
constexpr int CHUNK_H = 128;              // and 128 tall
constexpr int SECTION_H = 16;             // meshed per 16x16x16 section
constexpr int SECTIONS_PER_COLUMN = CHUNK_H / SECTION_H;  // 8

struct ChunkKey {
    int32_t cx = 0, cz = 0;
    bool operator==(const ChunkKey& o) const { return cx == o.cx && cz == o.cz; }
    bool operator!=(const ChunkKey& o) const { return !(*this == o); }
};

struct SectionKey {
    int32_t cx = 0, cz = 0;
    int32_t sy = 0;  // 0..SECTIONS_PER_COLUMN-1
    bool operator==(const SectionKey& o) const { return cx == o.cx && cz == o.cz && sy == o.sy; }
    bool operator!=(const SectionKey& o) const { return !(*this == o); }
    ChunkKey chunk() const { return {cx, cz}; }
};

inline size_t hashCombine(size_t h, size_t v) { return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)); }

// 12-byte vertex, see docs/ARCHITECTURE.md ("Vertex format"). Keep sizeof == 12.
struct MeshVertex {
    uint16_t x, y, z;    // position inside the section in 1/16 block units, 0..256 (section origin is a push constant)
    uint16_t tileFace;   // bits 0-7 atlas tile, bits 8-9 corner (0..3), bits 10-12 face (Face enum)
    uint8_t light;       // high nibble = sky light 0..15, low nibble = block light 0..15
    uint8_t ao;          // bits 0-1 ambient occlusion 0 (none) .. 3 (darkest); other bits reserved
    uint16_t flags;      // reserved, 0 (e.g. water height / tint / wave later)
};
static_assert(sizeof(MeshVertex) == 12, "MeshVertex must stay 12 bytes");

inline uint16_t packTileFace(int tile, int corner, int face) {
    return static_cast<uint16_t>(tile | (corner << 8) | (face << 10));
}

// Quads are 4 vertices + 6 uint16 indices (0,1,2, 0,2,3). A section has at most 49152 vertices, so uint16 is safe.
struct MeshLayer {
    std::vector<MeshVertex> vertices;
    std::vector<uint16_t> indices;
    bool empty() const { return indices.empty(); }
};

struct SectionMesh {
    SectionKey key;
    MeshLayer opaque;       // solid + cutout (leaves): depth write, alpha-test, no blend
    MeshLayer translucent;  // water + glass: blended, drawn after opaque, no depth write
    bool empty() const { return opaque.empty() && translucent.empty(); }
};

// mesh == nullptr (or empty) means "remove this section's GPU mesh".
struct MeshUpdate {
    SectionKey key;
    std::shared_ptr<const SectionMesh> mesh;
};

}  // namespace pc

namespace std {
template <> struct hash<pc::ChunkKey> {
    size_t operator()(const pc::ChunkKey& k) const noexcept {
        return pc::hashCombine(std::hash<int32_t>()(k.cx), std::hash<int32_t>()(k.cz));
    }
};
template <> struct hash<pc::SectionKey> {
    size_t operator()(const pc::SectionKey& k) const noexcept {
        return pc::hashCombine(pc::hashCombine(std::hash<int32_t>()(k.cx), std::hash<int32_t>()(k.cz)),
                               std::hash<int32_t>()(k.sy));
    }
};
}  // namespace std
