#include "storage.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace pc {
namespace {

constexpr uint32_t kColMagic = 0x48434350;  // "PCCH" little endian
constexpr uint16_t kColVersion = 1;
constexpr uint32_t kDatMagic = 0x44574350;  // "PCWD"

void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i))); }
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

bool writeAtomic(const std::string& path, const std::vector<uint8_t>& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fflush(f) == 0) && ok;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) { std::remove(tmp.c_str()); return false; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);  // atomic replace on POSIX
    if (ec) { std::remove(tmp.c_str()); return false; }
    return true;
}

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0 || n > (64 << 20)) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    bool ok = n == 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

}  // namespace

std::string columnPath(const std::string& dir, int cx, int cz) {
    return dir + "/c." + std::to_string(cx) + "." + std::to_string(cz) + ".bin";
}

bool saveColumnFile(const std::string& dir, int cx, int cz, const uint8_t* blocks) {
    std::vector<uint8_t> o;
    o.reserve(4096);
    put32(o, kColMagic);
    o.push_back(kColVersion & 255); o.push_back(kColVersion >> 8);
    put32(o, static_cast<uint32_t>(cx));
    put32(o, static_cast<uint32_t>(cz));
    int i = 0;
    while (i < kColumnCells) {
        uint8_t id = blocks[i];
        int j = i + 1;
        while (j < kColumnCells && blocks[j] == id) ++j;
        uint32_t run = static_cast<uint32_t>(j - i);
        o.push_back(id);
        while (run >= 128) { o.push_back(static_cast<uint8_t>(run | 128)); run >>= 7; }
        o.push_back(static_cast<uint8_t>(run));
        i = j;
    }
    return writeAtomic(columnPath(dir, cx, cz), o);
}

bool loadColumnFile(const std::string& dir, int cx, int cz, uint8_t* blocks) {
    std::vector<uint8_t> d;
    if (!readAll(columnPath(dir, cx, cz), d) || d.size() < 14) return false;
    if (get32(d.data()) != kColMagic) return false;
    if ((d[4] | (d[5] << 8)) != kColVersion) return false;
    if (static_cast<int32_t>(get32(&d[6])) != cx || static_cast<int32_t>(get32(&d[10])) != cz) return false;
    size_t p = 14;
    int n = 0;
    while (p < d.size()) {
        uint8_t id = d[p++];
        uint32_t run = 0;
        int shift = 0;
        for (;;) {
            if (p >= d.size() || shift > 21) return false;
            uint8_t b = d[p++];
            run |= static_cast<uint32_t>(b & 127) << shift;
            shift += 7;
            if (!(b & 128)) break;
        }
        if (run == 0 || n + static_cast<int>(run) > kColumnCells) return false;
        std::memset(blocks + n, id < BLOCK_COUNT ? id : static_cast<uint8_t>(AIR), run);
        n += static_cast<int>(run);
    }
    return n == kColumnCells;
}

bool readWorldDat(const std::string& dir, uint32_t& seed, std::string& blob) {
    std::vector<uint8_t> d;
    if (!readAll(dir + "/world.dat", d) || d.size() < 16 || get32(d.data()) != kDatMagic) return false;
    uint32_t len = get32(&d[12]);
    if (16 + static_cast<size_t>(len) > d.size()) return false;
    seed = get32(&d[8]);
    blob.assign(reinterpret_cast<const char*>(&d[16]), len);
    return true;
}

bool writeWorldDat(const std::string& dir, uint32_t seed, const std::string& blob) {
    std::vector<uint8_t> o;
    put32(o, kDatMagic); put32(o, 1); put32(o, seed); put32(o, static_cast<uint32_t>(blob.size()));
    o.insert(o.end(), blob.begin(), blob.end());
    return writeAtomic(dir + "/world.dat", o);
}

}  // namespace pc
