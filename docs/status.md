# Status

Each bot edits only its own section: what is done, how to use it, open requests for the architect.

## Bot 1 - architect
- Stage 0 done: interfaces (`engine/include`), game loop, player physics, raycast, break/place, thread pool, log, stubs (`world_stub.cpp`, `vulkan_renderer_stub.cpp`), JNI bridge, CMake, minimal Gradle project.
- Host check: `cmake -G Ninja engine -B /tmp/b && ninja -C /tmp/b` builds `pocketcraft_core`.
- Not done yet: player position persistence, day/night, held-button repeat breaking.

## Bot 2 - renderer
Done (`engine/render/`, replaces the stub):
- `vulkan_renderer.cpp`: Vulkan 1.0 `IRenderer` + `createVulkanRenderer()`. Android surface, FIFO present, 3 swapchain images, 2 frames in flight, D24/D32 depth (transient+lazy memory when offered, store DONT_CARE), swapchain recreate on OUT_OF_DATE/SUBOPTIMAL/resize, surface rotation handled by `preTransform = currentTransform` plus a 2D pre-rotation folded into the push-constant MVP.
- Meshes: 16 MB device-local blocks (one VkBuffer per block, first-fit free list with coalescing, empty blocks released), `uploadMesh` queues a CPU copy; `render()` uploads FIFO through a per-slot 4 MB staging buffer with a 2 MB/frame budget (always at least one section). Replaced/removed buffers are freed only after both frames that could use them finished. `stats()` reports sections and used GPU bytes.
- Culling: 6-plane frustum per section in camera-relative space (view translation stripped, so no precision loss far from origin), sections beyond `fogEnd + 14` skipped, opaque front-to-back, translucent back-to-front (no depth write, blended). 2 draws per section max.
- Shaders (`shaders/*.vert|frag`, GLSL 450): unpack of the 12-byte vertex, AO, sky/block light (sky light scaled by sky-colour luminance so day/night works from `FrameParams.sky`), per-face shade, linear fog to `sky`, alpha-test leaves (specialisation constant), water UV drift from `timeSeconds`. Crosshair (4 quads) and block outline (12 lines, 2 px if wideLines).
- `atlas.h/.cpp`: platform independent procedural 256x256 atlas with 5 mip levels (stops at 16x16 so tiles never bleed), deterministic. `buildAtlas(std::vector<uint8_t>&)`.
- `shaders.cmake`: glslc search in `${ANDROID_NDK}/shader-tools/<host>/` (also ANDROID_NDK_HOME, PATH, `-DPC_GLSLC=`), hard FATAL_ERROR if missing when `ANDROID` (or `-DPC_BUILD_SHADERS=ON`); skipped for the plain host build. SPIR-V emitted as `build/shaders/*.spv.inc` (`-mfmt=num`), target `pc_shaders` is added to `PC_RENDER_EXTRA_DEPS`.
Contract notes / requests for others:
- World (bot 3): atlas row 0 is V=0, so for side faces corner 0 (UV 0,0) must be the TOP-left corner when looking at the face (grass fringe, planks, log bark are painted upright). Face culling is OFF in the renderer because the quad winding is not specified; if the mesher guarantees CCW (viewed from outside) I can enable back-face culling: tell me.
- Architect: nothing required. `flags` (offset 10) is ignored by the shader for now.
Known gaps: no GLES fallback; no ambient/day-night tint beyond sky luminance; no anisotropic filtering; mirrored surface transforms unsupported; compaction/defrag of the suballocator not implemented; whole path compiled only on the host (never run on a GPU).

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
- Done (not compiled: no Android SDK in the container, checked by reading):
  - `MainActivity`: fullscreen immersive landscape, keep-screen-on only while playing, SurfaceView callbacks, lifecycle forwarding, back button opens/closes the pause menu, world `world1` with fixed seed 12345.
  - `ControlsView`: one full-screen overlay that tracks pointers by id. Left 40 % = joystick (dead zone 0.15), rest = look drag (tap < 200 ms = break, still > 350 ms = place), Jump and Sneak held, Break/Place/Fly pulses, MENU button top right, hotbar bottom centre in kHotbarBlocks order. Input goes to native at most once per frame (Choreographer).
  - `PauseMenuView`: Resume, render distance (2..16), look sensitivity (0.001..0.010 rad/px), FOV (50..110), debug line refreshed 2x/s.
  - `GameSettings`: SharedPreferences `pocketcraft_settings`, pushed with `nativeSetSetting` on change and on start.
  - `jni_bridge.cpp`: null checks, `ANativeWindow_fromSurface` null check, window released on surface destroy and on destroy, `nativeGetDebugInfo` copies the string before NewStringUTF. Syntax-checked with g++ against the JDK jni.h and stub Android headers.
  - Manifest: sensorLandscape, configChanges, label "Pocket Craft", framework `Theme.Material.NoActionBar.Fullscreen`. Adaptive vector launcher icon (green/brown block on blue).
- Not done: battery-saver (30 FPS) checkbox. The engine has no setting for it (only keys 0..2 exist), so it is left out.
- Input contract: break, place and fly are sent as true for one snapshot and false for the next. Jump and Sneak are held levels.
- Pause menu does not stop the game thread. It only sends neutral input and hides the controls.
- Open for the architect: a Sneak button was added beyond the spec, because fly-descend needs sneak. Engine setting for battery saver would be needed for that checkbox.
- Not compiled: Kotlin has not been compiled. Bot 5 should run the Android build first.

## Bot 5 - build/test
**APK in CI** (`.github/workflows/build-apk.yml`): every push runs `host-tests` (engine + tests on the host with ctest), then `apk` (`./gradlew assembleRelease`, `PC_VERSION_CODE` = run number). The APK is the workflow artifact `pocket-craft-apk`, file `pocket-craft-0.1.<run_number>.apk`. Pushes to `main` also publish GitHub Release `build-<run_number>` (marked latest) with the APK attached.

**Build locally**
- Tests: `cmake -S engine -B /tmp/pc-host -G Ninja && cmake --build /tmp/pc-host && ctest --test-dir /tmp/pc-host/tests --output-on-failure`
- APK: JDK 17, Android SDK under `ANDROID_HOME` with NDK 27.2.12479018 and CMake 3.22.1, then `cd android && ./gradlew assembleRelease`. Output: `android/app/build/outputs/apk/release/app-release.apk`. Set `PC_VERSION_CODE=<n>` (default 1); each new build needs a higher code to install over an old one.
- Signing: one fixed test key committed at `android/app/pocketcraft-debug.keystore` (alias and passwords `pocketcraft`), used by debug and release, so every APK installs as an update over the previous one. Not a store upload key.
- Gradle wrapper: 8.11.1.

**Tests** (`tests/`, own micro framework in `tests/test.h`, no downloads)
- math: floorDiv/floorMod with negatives, matrix identity and translate composition, perspective (centre, Y down, depth 0 and 1, frustum edge), lookAt.
- raycast: west, top and far faces with the right normal and place cell; misses beyond range and when pointing away.
- player: lands on a floor; cannot walk into a wall; jump apex about 1.4 blocks, then back on the ground. Built in the air at y 100-102 with `setBlock`, columns loaded via `isLoaded`.
- world: same seed gives the same blocks (sampled); set/get round trip incl. negative coords and out-of-range y; save and reload.
- No Game test (needs a fake `createVulkanRenderer()`; not worth it yet).

**Stub results** (host Debug build against `world_stub.cpp`): 14 of 15 pass. `world_save_and_reload_round_trip` fails, since the stub keeps edits in memory only. The determinism test passes trivially against the flat stub terrain. The APK build itself has not been run (no Android SDK here).
