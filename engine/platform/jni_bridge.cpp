// JNI glue. Kotlin side: com.pocketcraft.game.NativeBridge (object with @JvmStatic external funs).
// All calls arrive on the Android UI thread. This file owns the ANativeWindow reference.
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <memory>
#include <string>

#include "game.h"
#include "blocks.h"
#include "atlas.h"  // engine/render is an include dir for the platform target (see engine/CMakeLists.txt)

#include <vector>

namespace {
std::unique_ptr<pc::Game> g_game;
ANativeWindow* g_window = nullptr;

// Level-0 RGBA8 atlas, built once. Needs no Game, so the home screen and the inventory can call it early.
const std::vector<uint8_t>& atlasLevel0() {
    static const std::vector<uint8_t> rgba = [] {
        std::vector<uint8_t> all;
        pc::buildAtlas(all);
        all.resize(pc::atlasMipBytes(0));
        return all;
    }();
    return rgba;
}

void releaseWindow() {
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}
}  // namespace

#define JNI_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_pocketcraft_game_NativeBridge_##name

JNI_FN(void, nativeOnCreate)(JNIEnv* env, jclass, jstring filesDir, jstring worldName, jlong seed) {
    if (!filesDir || !worldName) return;
    const char* dir = env->GetStringUTFChars(filesDir, nullptr);
    const char* name = env->GetStringUTFChars(worldName, nullptr);
    if (dir && name) {
        if (g_game) g_game->onDestroy();  // should not happen; saves the previous instance
        g_game = std::make_unique<pc::Game>();
        g_game->onCreate(dir, name, static_cast<uint32_t>(seed));
    }
    if (dir) env->ReleaseStringUTFChars(filesDir, dir);
    if (name) env->ReleaseStringUTFChars(worldName, name);
}

JNI_FN(void, nativeOnSurfaceCreated)(JNIEnv* env, jclass, jobject surface) {
    if (!g_game || !surface) return;
    releaseWindow();
    g_window = ANativeWindow_fromSurface(env, surface);
    if (g_window) g_game->onSurfaceCreated(g_window);
}

JNI_FN(void, nativeOnSurfaceChanged)(JNIEnv*, jclass, jint w, jint h) {
    if (g_game) g_game->onSurfaceChanged(w, h);
}

JNI_FN(void, nativeOnSurfaceDestroyed)(JNIEnv*, jclass) {
    if (g_game) g_game->onSurfaceDestroyed();  // game thread is stopped before this returns
    releaseWindow();
}

JNI_FN(void, nativeOnPause)(JNIEnv*, jclass) {
    if (g_game) g_game->onPause();
}

JNI_FN(void, nativeOnResume)(JNIEnv*, jclass) {
    if (g_game) g_game->onResume();
}

JNI_FN(void, nativeOnDestroy)(JNIEnv*, jclass) {
    if (g_game) g_game->onDestroy();
    g_game.reset();
    releaseWindow();
}

JNI_FN(void, nativeSetInput)(JNIEnv*, jclass, jfloat moveX, jfloat moveZ, jfloat lookDx, jfloat lookDy, jboolean jump,
                             jboolean sneak, jboolean breakPressed, jboolean placePressed, jboolean flyToggle,
                             jint hotbarSlot) {
    if (!g_game) return;
    pc::InputState in;
    in.moveX = moveX;
    in.moveZ = moveZ;
    in.lookDX = lookDx;
    in.lookDY = lookDy;
    in.jump = jump;
    in.sneak = sneak;
    in.breakPressed = breakPressed;
    in.placePressed = placePressed;
    in.flyToggle = flyToggle;
    in.hotbarSlot = hotbarSlot;
    g_game->setInput(in);
}

JNI_FN(void, nativeSetSetting)(JNIEnv*, jclass, jint key, jfloat value) {
    if (g_game) g_game->setSetting(key, value);
}

JNI_FN(jstring, nativeGetDebugInfo)(JNIEnv* env, jclass) {
    if (!g_game) return env->NewStringUTF("");
    const std::string info = g_game->debugInfo();  // copy: must outlive the NewStringUTF call
    return env->NewStringUTF(info.c_str());
}


JNI_FN(void, nativeSetHotbarSlot)(JNIEnv*, jclass, jint slot, jint blockId) {
    if (!g_game || slot < 0 || slot >= 9) return;
    if (blockId < 0 || blockId >= pc::BLOCK_COUNT || !pc::isPlaceable(static_cast<uint8_t>(blockId))) return;
    g_game->setHotbarSlot(slot, blockId);
}

JNI_FN(jint, nativeGetHotbarSlot)(JNIEnv*, jclass, jint slot) {
    if (slot < 0 || slot >= 9) return pc::AIR;
    if (!g_game) return pc::kHotbarBlocks[slot];  // before nativeOnCreate: the engine default
    return g_game->getHotbarSlot(slot);
}

JNI_FN(jintArray, nativeGetPlaceableBlocks)(JNIEnv* env, jclass) {
    std::vector<jint> ids;
    for (int id = 1; id < pc::BLOCK_COUNT; ++id) {
        if (pc::isPlaceable(static_cast<uint8_t>(id))) ids.push_back(id);
    }
    const jsize n = static_cast<jsize>(ids.size());
    jintArray arr = env->NewIntArray(n);
    if (arr && n > 0) env->SetIntArrayRegion(arr, 0, n, ids.data());
    return arr;
}

JNI_FN(jstring, nativeGetBlockName)(JNIEnv* env, jclass, jint id) {
    if (id < 0 || id >= pc::BLOCK_COUNT) return env->NewStringUTF("");
    return env->NewStringUTF(pc::kBlockInfo[id].name);
}

JNI_FN(jint, nativeGetBlockIconTile)(JNIEnv*, jclass, jint id) {
    if (id < 0 || id >= pc::BLOCK_COUNT) return 0;
    return pc::kBlockInfo[id].tileSide;
}

JNI_FN(jbyteArray, nativeGetAtlasRGBA)(JNIEnv* env, jclass) {
    const std::vector<uint8_t>& px = atlasLevel0();
    const jsize n = static_cast<jsize>(px.size());
    jbyteArray arr = env->NewByteArray(n);
    if (arr) env->SetByteArrayRegion(arr, 0, n, reinterpret_cast<const jbyte*>(px.data()));
    return arr;
}
