# Status

Each bot edits only its own section: what is done, how to use it, open requests for the architect.

## Bot 1 - architect
- Stage 0 done: interfaces (`engine/include`), game loop, player physics, raycast, break/place, thread pool, log, stubs (`world_stub.cpp`, `vulkan_renderer_stub.cpp`), JNI bridge, CMake, minimal Gradle project.
- Host check: `cmake -G Ninja engine -B /tmp/b && ninja -C /tmp/b` builds `pocketcraft_core`.
- Not done yet: player position persistence, day/night, held-button repeat breaking.

## Bot 2 - renderer
(todo)

## Bot 3 - world
(todo; delete `engine/world/world_stub.cpp` when the real World exists)

## Bot 4 - android
(todo)

## Bot 5 - build/test
(todo)
