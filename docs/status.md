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
