// Input snapshot pushed from the Android UI thread. Kotlin sends one on every change (and at most every ~16 ms).
#pragma once

namespace pc {

struct InputState {
    float moveX = 0;   // strafe, -1 (left) .. +1 (right)
    float moveZ = 0;   // forward, -1 (back) .. +1 (forward)
    float lookDX = 0;  // touch-drag delta in pixels since the previous snapshot (ACCUMULATED natively)
    float lookDY = 0;  // positive = finger moved down = look down
    bool jump = false;          // held level
    bool sneak = false;         // held level; descends while flying
    bool breakPressed = false;  // level; the game acts on the false->true edge
    bool placePressed = false;  // level; the game acts on the false->true edge
    bool flyToggle = false;     // level; edge toggles flying
    int hotbarSlot = 0;         // 0..8
};

}  // namespace pc
