// Internal: column files and world.dat. All functions are thread-safe only if the caller serialises writes.
#pragma once
#include <cstdint>
#include <string>

#include "column.h"

namespace pc {

std::string columnPath(const std::string& dir, int cx, int cz);
// Atomic (temp + rename). Header: "PCCH", u16 version, i32 cx, i32 cz, then RLE of blocks (u8 id, varint run).
bool saveColumnFile(const std::string& dir, int cx, int cz, const uint8_t* blocks);
// False if the file is missing or invalid (caller then generates).
bool loadColumnFile(const std::string& dir, int cx, int cz, uint8_t* blocks);

// world.dat: "PCWD", u32 version, u32 seed, u32 blobLen, blob.
bool readWorldDat(const std::string& dir, uint32_t& seed, std::string& blob);
bool writeWorldDat(const std::string& dir, uint32_t seed, const std::string& blob);

}  // namespace pc
