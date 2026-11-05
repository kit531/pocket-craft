# Pocket Craft architecture

Original block game for Android phones. C++17 engine + thin Kotlin layer, Vulkan 1.0 renderer (GLES3 fallback later), no third-party C++ deps.

## Goals and budgets
- Battery: game thread runs only while a surface exists and the app is resumed; frame cap 60 FPS even on 120 Hz panels; no busy loops.
- Memory: < 512 MB total (native + Java + GPU). Chunk data 16x16x128 bytes = 32 KB per column; render distance 8 => ~800 columns max ~ 26 MB blocks, meshes extra.
- 60 FPS on Galaxy S25 Ultra, playable on mid phones. Vertex = 12 bytes, 2 draw calls per section at most, frustum culling per section.
- Single-player first; `World` has no rendering/input/Android dependency so a server can reuse it.

## Folder map and ownership
| Folder | Owner | Contents |
|---|---|---|
| `engine/include/**`, `engine/core/**`, `engine/game/**`, `engine/CMakeLists.txt`, `docs/**` | Bot 1 architect | shared interfaces, math, thread pool, log, game loop, player physics, raycast, break/place |
| `engine/render/**` | Bot 2 renderer | Vulkan backend, shaders (`render/shaders/`, `render/shaders.cmake`), procedural atlas, camera/frustum culling |
| `engine/world/**` | Bot 3 world | chunks, terrain gen, meshing, worker pool use, saving (replaces `world_stub.cpp`) |
| `android/app/src/main/**`, `engine/platform/**` | Bot 4 android | Kotlin Activity, touch controls, menus, `NativeBridge.kt`, JNI C++ (`jni_bridge.cpp`) |
| `.github/**`, `tests/**`, `android/*.kts`, `android/gradle*`, `android/app/build.gradle.kts`, `android/app/proguard-rules.pro`, wrapper, signing | Bot 5 build/test | CI, host tests, gradle config |

Rules: only touch your own folders. Need an interface change (anything in `engine/include`)? Write a request in `docs/status.md` under your section; the architect applies it. Sources are globbed by CMake (`core game world render platform`), so adding a .cpp needs no CMake edit. Host build (`pocketcraft_core`, static, core+game+world) must stay free of Android/Vulkan headers; `core/log.cpp` is the only place allowed `#ifdef __ANDROID__`. Tests that link `pocketcraft_core` must supply their own `createVulkanRenderer()` (fake) if they instantiate `Game`.

## Coordinate system
Y up, right-handed. Block coords are ints; block (x,y,z) occupies [x,x+1) etc. Valid y: 0..127. Chunk key = (cx, cz) with cx = floorDiv(x,16) (use `floorDiv/floorMod` from pc_math.h, never `/ %` on negatives). Section = (cx, cz, sy), sy = y/16. Yaw 0 looks toward -Z, increasing yaw turns right; forward = (sin y cos p, sin p, -cos y cos p). Matrices are column-major; `proj` is Vulkan clip space (Y down, depth 0..1), passed to the renderer as is. Face order: +X, -X, +Y, -Y, +Z, -Z.

## Threading model
- UI thread (Kotlin): lifecycle, touch -> `nativeSetInput`. Brief locks only.
- Game thread (one, owned by `Game`): created when surface exists AND resumed AND size known; joined on surface destroy/pause. Creates the renderer, runs fixed 60 Hz ticks (max 5 catch-up), then per frame: apply look/actions, upload meshes (<=12/frame), render. All World and IRenderer calls happen here.
- World workers (2, `ThreadPool` in `core`): terrain gen and meshing, owned by World. Finished meshes are pushed to a thread-safe queue; the game thread drains it with `pollMeshUpdates` and uploads. Workers must never touch the renderer.
- On surface loss the renderer is destroyed; on recreate `World::remeshAll()` re-sends every mesh (World keeps no CPU copy of sent meshes).

## Data flow
touch -> Kotlin `NativeBridge.nativeSetInput` -> `Game::setInput` (latched edges, accumulated look) -> tick: `Player::update` (collision via `World::getBlock`), `World::update(playerPos)` -> `setBlock` on break/place -> World re-meshes dirty sections on workers -> `MeshUpdate` queue -> `Game` -> `IRenderer::uploadMesh/removeMesh` -> `IRenderer::render(FrameParams)`.

## Interfaces (engine/include)
- `pc_math.h`: `Vec2/Vec3/Vec3i/Mat4`, `perspective`, `lookAt`, `multiply`, `translate`, `transform`, `floorDiv/floorMod`.
- `blocks.h`: `BlockId`, `Tile` (atlas indices, 16x16 grid in a 256x256 atlas, index = row*16+col), `BlockInfo` {solid, transparent, liquid, tiles top/bottom/side}, `blockInfo(id)`, `tileForFace`, `kHotbarBlocks`. Ids are stored on disk: append only.
- `chunk.h`: constants (`CHUNK_W=16, CHUNK_H=128, SECTION_H=16`), `ChunkKey`, `SectionKey` (+ std::hash), `MeshVertex`, `MeshLayer`, `SectionMesh`, `MeshUpdate`.
- `world.h` `World(seed, saveDir)`: `update(playerPos)` cheap, never blocks on workers; `getBlock` (air above, bedrock below, air if unloaded); `setBlock` (false if unloaded; marks column dirty, remeshes section + border neighbours); `isLoaded(cx,cz)`; `topSolidY`; `pollMeshUpdates(out)` thread-safe; `remeshAll`; `saveAll` blocking; `setRenderDistance` any thread. Game thread is the only caller of the rest.
- `renderer.h` `IRenderer`: `init(void* ANativeWindow*)->bool`, `resize`, `uploadMesh(const SectionMesh&)` (copies, replaces same key; empty = remove), `removeMesh`, `render(const FrameParams&)` (world + sky/fog + crosshair + selection outline, then present; may block for vsync), `stats`, `shutdown`. `FrameParams`: view, proj, viewProj, cameraPos, sky rgb, fogStart/End, timeSeconds, hasSelection + selectedBlock. Factory `createVulkanRenderer()`.
- `input.h` `InputState`: moveX/moveZ (-1..1), lookDX/lookDY (pixels since last snapshot, accumulated natively), jump, sneak, breakPressed, placePressed, flyToggle (levels; game acts on false->true edge, so Kotlin must send true then a later false), hotbarSlot 0..8.
- `game.h` `Game`: `onCreate(filesDir, worldName, seed)`, `onSurfaceCreated(window)`, `onSurfaceChanged(w,h)`, `onSurfaceDestroyed`, `onPause`, `onResume`, `onDestroy`, `setInput`, `setSetting(key,value)` (0 render distance 2..16, 1 look sensitivity rad/px default 0.004, 2 FOV deg default 70, 3 frame cap 30|60 default 60), `debugInfo()`.
- `raycast.h`: `raycast(origin, dir, maxDist, BlockGetter)` DDA over solid blocks, returns block + face normal. Reach 6 blocks.
- `thread_pool.h`: `ThreadPool(n)`, `ThreadSafeQueue<T>`. `pc_log.h`: `PC_LOGD/I/W/E(fmt, ...)`.

## Vertex format (`MeshVertex`, 12 bytes, `uint16_t indices`, 4 verts + 6 indices per quad)
| Offset | Type | Field |
|---|---|---|
| 0 | u16 | x: position in the section in 1/16 block (0..256) |
| 2 | u16 | y (same) |
| 4 | u16 | z (same) |
| 6 | u16 | tileFace: bits 0-7 atlas tile, bits 8-9 corner 0..3, bits 10-12 face (Face enum) |
| 8 | u8 | light: high nibble sky light, low nibble block light (0..15) |
| 9 | u8 | ao: bits 0-1 ambient occlusion (0 none .. 3 darkest) |
| 10 | u16 | flags: reserved, 0 |

Vulkan attributes: loc0 `R16G16B16A16_UINT` @0 (x,y,z,tileFace), loc1 `R8G8_UINT` @8 (light, ao), loc2 `R16_UINT` @10 (flags); stride 12. Corner 0..3 maps to tile UV (0,0),(1,0),(1,1),(0,1). Orientation contract (mesher and atlas agree): on the four side faces corner 0 is the TOP-left seen from outside, 1 top-right, 2 bottom-right, 3 bottom-left (atlas row 0 = V 0 = top of the picture); top/bottom faces use -Z / +Z as "up". Quads are CLOCKWISE seen from outside in the Y-up view; the Y-flipped projection keeps that on screen, so the renderer uses `VK_FRONT_FACE_CLOCKWISE` + back-face culling (translucent layer too). the shader computes UV = (tile%16 + u)/16 etc, normal from face. Position in the shader = origin + xyz/16 where origin (`ivec3` section origin relative to camera, or float3) is a push constant. A section has at most 49152 vertices so u16 indices are safe. Meshing: skip faces whose neighbour is opaque or the same transparent block id. Two layers per section: `opaque` (incl. leaves, alpha-tested via atlas alpha) and `translucent` (water, glass; blended, no depth write, drawn second).

## Blocks (ids)
0 air, 1 grass, 2 dirt, 3 stone, 4 sand, 5 water, 6 log, 7 leaves, 8 planks, 9 glass, 10 cobble, 11 bedrock. Tiles: grass_top 0, grass_side 1, dirt 2, stone 3, sand 4, water 5, log_top 6, log_side 7, leaves 8, planks 9, glass 10, cobble 11, bedrock 12 (alpha 0 where transparent: leaves, glass edges). Hotbar slots 0..8 place: grass, dirt, stone, cobble, planks, log, sand, glass, leaves.

## JNI (Kotlin `object com.pocketcraft.game.NativeBridge`, every function `@JvmStatic external`, lib `pocketcraft`)
```
nativeOnCreate(filesDir: String, worldName: String, seed: Long)
nativeOnSurfaceCreated(surface: android.view.Surface)
nativeOnSurfaceChanged(width: Int, height: Int)
nativeOnSurfaceDestroyed()
nativeOnPause()
nativeOnResume()
nativeOnDestroy()
nativeSetInput(moveX: Float, moveZ: Float, lookDx: Float, lookDy: Float, jump: Boolean, sneak: Boolean,
               breakPressed: Boolean, placePressed: Boolean, flyToggle: Boolean, hotbarSlot: Int)
nativeSetSetting(key: Int, value: Float)
nativeGetDebugInfo(): String
```
C++ names: `Java_com_pocketcraft_game_NativeBridge_<fn>(JNIEnv*, jclass, ...)`. Call order from the Activity: nativeOnCreate (Activity.onCreate), surface callbacks, nativeOnResume/Pause, nativeOnDestroy. The bridge owns the `ANativeWindow*` and releases it after `onSurfaceDestroyed`. Saves: per chunk column file under `<filesDir>/worlds/<name>/`, only modified columns, written on pause/destroy and when modified columns unload.
