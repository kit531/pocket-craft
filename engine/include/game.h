// Game: owns World + IRenderer + player, runs the single game thread (60 Hz fixed update, render every frame).
// The lifecycle methods are called from the Android UI thread and may block briefly (thread join).
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "input.h"

namespace pc {

enum Setting : int {
    SETTING_RENDER_DISTANCE = 0,  // chunks radius, 2..16, default 8
    SETTING_LOOK_SENSITIVITY = 1, // radians per touch pixel, default 0.004
    SETTING_FOV = 2,              // vertical degrees, default 70
    SETTING_FRAME_CAP = 3,        // 30 (battery saver) or 60 (default)
};

class Game {
public:
    Game();
    ~Game();
    // Creates the World at <filesDir>/worlds/<worldName>. Call once before anything else.
    void onCreate(const std::string& filesDir, const std::string& worldName, uint32_t seed);
    void onSurfaceCreated(void* nativeWindow);  // ANativeWindow*; caller keeps it alive until onSurfaceDestroyed returns
    void onSurfaceChanged(int width, int height);
    void onSurfaceDestroyed();                  // stops the game thread and shuts the renderer down before returning
    void onPause();                             // stops the game thread, saves the world
    void onResume();
    void onDestroy();                           // saves and frees the world
    void setInput(const InputState& in);        // any thread
    void setSetting(int key, float value);      // any thread
    void setHotbarSlot(int slot, int blockId);  // any thread; slot 0..8, blockId must be placeable (isPlaceable); else ignored
    int getHotbarSlot(int slot) const;          // any thread; block id of the slot, or 0 for an invalid slot
    std::string debugInfo() const;              // one line: fps, position, sections; any thread

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pc
