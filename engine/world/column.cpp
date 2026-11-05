#include "column.h"

#include <cstring>
#include <vector>

namespace pc {

void computeSkyLight(Column& c) {
    static thread_local std::vector<uint16_t> queue;
    queue.clear();
    std::memset(c.sky, 0, sizeof(c.sky));
    bool opaque[256];
    for (int i = 0; i < 256; ++i) opaque[i] = blockOpaque(static_cast<uint8_t>(i));
    // Vertical pass.
    for (int z = 0; z < CHUNK_W; ++z)
        for (int x = 0; x < CHUNK_W; ++x) {
            int cur = 15;
            for (int y = CHUNK_H - 1; y >= 0; --y) {
                int i = Column::idx(x, y, z);
                uint8_t b = c.blocks[i];
                if (opaque[b]) { cur = 0; continue; }
                cur -= skyAbsorb(b);
                if (cur < 0) cur = 0;
                c.setSky(i, cur);
            }
        }
    // Horizontal/vertical flood inside the column. Seed with cells that have a darker open neighbour.
    static const int dx[6] = {1, -1, 0, 0, 0, 0}, dy[6] = {0, 0, 1, -1, 0, 0}, dz[6] = {0, 0, 0, 0, 1, -1};
    for (int y = 0; y < CHUNK_H; ++y)
        for (int z = 0; z < CHUNK_W; ++z)
            for (int x = 0; x < CHUNK_W; ++x) {
                int i = Column::idx(x, y, z);
                int l = c.skyAt(i);
                if (l < 2) continue;
                for (int d = 0; d < 6; ++d) {
                    int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
                    if (nx < 0 || nx > 15 || nz < 0 || nz > 15 || ny < 0 || ny >= CHUNK_H) continue;
                    int ni = Column::idx(nx, ny, nz);
                    if (!opaque[c.blocks[ni]] && c.skyAt(ni) < l - 1) { queue.push_back(static_cast<uint16_t>(i)); break; }
                }
            }
    // queue holds linear indices < 32768 which fit in uint16_t.
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        int i = queue[qi];
        int l = c.skyAt(i);
        if (l < 2) continue;
        int x = i & 15, z = (i >> 4) & 15, y = i >> 8;
        for (int d = 0; d < 6; ++d) {
            int nx = x + dx[d], ny = y + dy[d], nz = z + dz[d];
            if (nx < 0 || nx > 15 || nz < 0 || nz > 15 || ny < 0 || ny >= CHUNK_H) continue;
            int ni = Column::idx(nx, ny, nz);
            if (opaque[c.blocks[ni]] || c.skyAt(ni) >= l - 1) continue;
            c.setSky(ni, l - 1);
            queue.push_back(static_cast<uint16_t>(ni));
        }
    }
}

void finalizeColumn(Column& c) {
    for (int s = 0; s < SECTIONS_PER_COLUMN; ++s) {
        int n = 0;
        const uint8_t* p = c.blocks + s * 4096;
        for (int i = 0; i < 4096; ++i) n += p[i] != AIR;
        c.nonAir[s] = static_cast<uint16_t>(n);
    }
    for (int z = 0; z < CHUNK_W; ++z)
        for (int x = 0; x < CHUNK_W; ++x) {
            int y = CHUNK_H - 1;
            while (y >= 0 && c.blocks[Column::idx(x, y, z)] == AIR) --y;
            c.top[(z << 4) | x] = static_cast<uint8_t>(y + 1);
        }
    computeSkyLight(c);
}

}  // namespace pc
