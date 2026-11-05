// Helpers shared by the world/player tests: temp dirs and waiting for chunks to load.
#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "chunk.h"
#include "math.h"
#include "world.h"

namespace pctest {

// Unique directory under the system temp dir, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& label) {
        static int counter = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("pocketcraft_test_" + label + "_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    std::string path() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

// Calls world.update(center) until every column covering the block box [x0,x1]x[z0,z1] reports isLoaded.
// Returns false on timeout. With a stub world this returns immediately.
inline bool waitLoaded(pc::World& world, const pc::Vec3& center, int x0, int x1, int z0, int z1,
                       int timeoutMs = 20000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        world.update(center);
        bool all = true;
        for (int cz = pc::floorDiv(z0, pc::CHUNK_W); cz <= pc::floorDiv(z1, pc::CHUNK_W); ++cz)
            for (int cx = pc::floorDiv(x0, pc::CHUNK_W); cx <= pc::floorDiv(x1, pc::CHUNK_W); ++cx)
                if (!world.isLoaded(cx, cz)) all = false;
        if (all) return true;
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

}  // namespace pctest
