# Status

Each bot edits only its own section: what is done, how to use it, open requests for the architect.

## Bot 1 - architect
- Stage 0 done: interfaces (`engine/include`), game loop, player physics, raycast, break/place, thread pool, log, stubs (`world_stub.cpp`, `vulkan_renderer_stub.cpp`), JNI bridge, CMake, minimal Gradle project.
- Host check: `cmake -G Ninja engine -B /tmp/b && ninja -C /tmp/b` builds `pocketcraft_core`.
- Not done yet: player position persistence, day/night, held-button repeat breaking.

## Bot 2 - renderer
(todo)

## Bot 3 - world
Done (`engine/world/`, `world_stub.cpp` deleted):
- Storage: `Column` = 32 KB blocks + 16 KB nibble sky light + per-section non-air counts (empty sections are never meshed) + top-y map; ~49 KB/column. Game thread owns all columns; workers only see freshly generated columns, immutable 18^3 snapshots (blocks + sky light of the 3x3 neighbour columns, copied on the game thread) or unloaded columns being saved. No locks on block data.
- Streaming: `update()` is a no-op unless the player chunk/radius changed or a job finished; then it scans a pre-sorted circle nearest first. Generation circle = R+2, meshing circle = R (a column is meshed only when all 8 neighbours are loaded, so borders are always correct), unload beyond R+3 (modified columns are saved on the pool; a reload before the save lands reads the in-memory copy). At most 6 jobs in flight; edits (`setBlock`) bypass the cap.
- Staleness: each section has an atomic version; mesh results carry the version they were built from and `pollMeshUpdates` drops stale ones. Unload emits removal updates (mesh == nullptr) only for sections the renderer actually has.
- Terrain: value-noise fbm heightmap (continent + ruggedness -> plains/hills/mountains), sea level 40, sand beaches (h <= 42) and shallow sea floors, grass/dirt/stone, bedrock y=0 (+50% y=1), 4-block lattice tunnel caves (y 3..63, never within 4 blocks of the surface), trees (log + leaves) evaluated per neighbouring world column so border trees are seamless and order independent. All deterministic from the seed.
- Meshing: hidden-face culling (opaque neighbour or same transparent id), classic 3-neighbour AO with diagonal flip, smooth sky light (average of open cells around the vertex, light in the high nibble, block light 0), opaque layer (incl. leaves) vs translucent layer (water, glass; no AO), water tops lowered to 14/16. No greedy merging.
- setBlock: updates data, counts, top map; recomputes column sky light only when opacity/absorption changed and re-meshes every section whose snapshot sees the changed cells (including diagonal neighbours, other columns, light changes).
- Saving: `<saveDir>/c.<cx>.<cz>.bin` ("PCCH", u16 version 1, cx, cz, RLE of ids with varint run), temp file + rename; only modified columns; `saveAll()` also flushes in-flight unload saves. Invalid/corrupt file => column is regenerated. `world.dat` ("PCWD": seed + opaque blob) is written on creation/saveAll; an existing world.dat seed overrides the seed passed to the constructor.
- Winding contract for the renderer: quads are clockwise seen from outside in a Y-up view (corner 0 = top-left, 1 TR, 2 BR, 3 BL; top/bottom faces use -Z / +Z as "up") => `VK_FRONT_FACE_CLOCKWISE` after the Y-flipped projection. Indices (0,1,2,0,2,3) or the flipped diagonal.
- Host measurements (-O2, one core of this box): terrain+light 0.45 ms/column, meshing a whole column (8 sections) ~0.4 ms, so gen+mesh ~1 ms/column; radius 8 = 317 loaded columns = 15 MB block+light data; full radius-8 load incl. meshing in ~0.4 s on 2 threads (710k vertices ~ 10 MB transient, the World keeps no mesh copies); worst `update()+pollMeshUpdates()` while streaming after a far teleport 0.9 ms. Radius 16 estimate: ~1100 columns ~ 54 MB.
- Verified with a throwaway host program (not committed): determinism per seed, every emitted quad has a solid block behind it and a different transparent neighbour in front (177k quads incl. chunk borders), tile ids, far teleport unloads/removes old meshes, setBlock (border, lighting roof, glass) + saveAll + reload in a new World round trip, modified-column unload and reload; clean under -fsanitize=address,undefined and -fsanitize=thread; zero warnings with -Wall -Wextra.

Known gaps / notes:
- Sky light is computed per column; light does not flood across column borders (visible only as slight seams under overhangs at borders). No block light yet (low nibble 0). No night/day (shader scales sky light).
- Mountains are small and rare, no snow, no rivers, no ores, caves never open to the surface.
- `pollMeshUpdates` is thread-safe for the queue but should be called from the game thread (same thread as update/unload) to keep the "sent" bookkeeping exact.
- Request to architect (optional): expose the world.dat blob, e.g. `bool World::getExtra(std::string&) const; void World::setExtra(std::string)`; the file format already stores it (needed for player position later).
- Request to renderer: terrain mesh frontface = clockwise, see winding contract above.

## Bot 4 - android
(todo)

## Bot 5 - build/test
(todo)
