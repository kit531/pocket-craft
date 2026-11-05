// STUB World: infinite flat terrain (grass at y=40), edits kept in memory, never produces meshes.
// The world bot replaces this file with the real implementation (delete it when done).
#include "world.h"

#include <unordered_map>

#include "blocks.h"

namespace pc {

struct World::Impl {
    uint32_t seed;
    std::unordered_map<uint64_t, uint8_t> edits;
    static uint64_t pack(int x, int y, int z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) ^
               (static_cast<uint64_t>(static_cast<uint32_t>(z)) << 8) ^ static_cast<uint64_t>(y);
    }
};

World::World(uint32_t seed, std::string) : impl_(new Impl) { impl_->seed = seed; }
World::~World() = default;
void World::setRenderDistance(int) {}
void World::update(const Vec3&) {}
bool World::isLoaded(int, int) const { return true; }
int World::topSolidY(int, int) const { return 40; }
size_t World::pollMeshUpdates(std::vector<MeshUpdate>&) { return 0; }
void World::remeshAll() {}
void World::saveAll() {}
uint32_t World::seed() const { return impl_->seed; }

uint8_t World::getBlock(int x, int y, int z) const {
    if (y < 0) return BEDROCK;
    if (y >= CHUNK_H) return AIR;
    auto it = impl_->edits.find(Impl::pack(x, y, z));
    if (it != impl_->edits.end()) return it->second;
    return y < 40 ? STONE : (y == 40 ? GRASS : AIR);
}

bool World::setBlock(int x, int y, int z, uint8_t id) {
    if (y < 0 || y >= CHUNK_H) return false;
    impl_->edits[Impl::pack(x, y, z)] = id;
    return true;
}

}  // namespace pc
