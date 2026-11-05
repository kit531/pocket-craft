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
(todo; delete `engine/world/world_stub.cpp` when the real World exists)

## Bot 4 - android
(todo)

## Bot 5 - build/test
(todo)
