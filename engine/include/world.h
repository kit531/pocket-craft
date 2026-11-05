// World: block storage, terrain generation, meshing, saving. Implemented in engine/world (no rendering, no input,
// no Android) so a server can reuse it. All methods except pollMeshUpdates/internal workers are called from ONE
// thread (the game thread); the implementation owns its worker threads and must make that safe.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "chunk.h"
#include "math.h"

namespace pc {

class World {
public:
    World(uint32_t seed, std::string saveDir);  // saveDir = <filesDir>/worlds/<name>; created if missing
    ~World();                                   // joins workers; does NOT save (call saveAll)
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void setRenderDistance(int chunks);   // any thread; radius in chunks, clamped 2..16, default 8
    // Called every tick: load/generate/mesh columns around the player, unload (and save if modified) far ones.
    // Must be cheap (no blocking on workers).
    void update(const Vec3& playerPos);

    // Air above the world, BEDROCK below it, AIR for unloaded columns (check isLoaded when it matters).
    uint8_t getBlock(int x, int y, int z) const;
    // False if out of range or the column is not loaded. Marks the column modified and queues re-meshing of the
    // section and of neighbouring sections when the block is on a section border.
    bool setBlock(int x, int y, int z, uint8_t id);
    bool isLoaded(int cx, int cz) const;  // generated/loaded and usable by getBlock
    // Y of the highest non-air block, or -1 if the column is not loaded.
    int topSolidY(int x, int z) const;

    // Moves finished meshes into `out` (appends). Thread-safe, non-blocking. Returns number appended.
    size_t pollMeshUpdates(std::vector<MeshUpdate>& out);
    // Queue a re-mesh of every loaded section (used when the renderer lost its GPU buffers, e.g. surface recreated).
    void remeshAll();

    void saveAll();  // writes modified columns, blocking; call on pause/exit
    uint32_t seed() const;
    // Opaque blob stored in world.dat (written by saveAll). Game thread only; used for player position.
    const std::string& getExtra() const;
    void setExtra(std::string blob);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pc
