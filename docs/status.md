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
(todo)
