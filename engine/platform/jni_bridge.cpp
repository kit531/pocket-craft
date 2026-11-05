// JNI glue. Kotlin side: com.pocketcraft.game.NativeBridge (object with @JvmStatic external funs).
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <memory>
#include <string>

#include "game.h"
#include "log.h"

namespace {
std::unique_ptr<pc::Game> g_game;
ANativeWindow* g_window = nullptr;
}  // namespace

#define JNI_FN(ret, name) extern "C" JNIEXPORT ret JNICALL Java_com_pocketcraft_game_NativeBridge_##name

JNI_FN(void, nativeOnCreate)(JNIEnv* env, jclass, jstring filesDir, jstring worldName, jlong seed) {
    const char* dir = env->GetStringUTFChars(filesDir, nullptr);
    const char* name = env->GetStringUTFChars(worldName, nullptr);
    g_game = std::make_unique<pc::Game>();
    g_game->onCreate(dir, name, static_cast<uint32_t>(seed));
    env->ReleaseStringUTFChars(filesDir, dir);
    env->ReleaseStringUTFChars(worldName, name);
}

JNI_FN(void, nativeOnSurfaceCreated)(JNIEnv* env, jclass, jobject surface) {
    if (!g_game) return;
    if (g_window) ANativeWindow_release(g_window);
    g_window = ANativeWindow_fromSurface(env, surface);
    g_game->onSurfaceCreated(g_window);
}

JNI_FN(void, nativeOnSurfaceChanged)(JNIEnv*, jclass, jint w, jint h) {
    if (g_game) g_game->onSurfaceChanged(w, h);
}

JNI_FN(void, nativeOnSurfaceDestroyed)(JNIEnv*, jclass) {
    if (g_game) g_game->onSurfaceDestroyed();  // game thread is stopped before this returns
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
}

JNI_FN(void, nativeOnPause)(JNIEnv*, jclass) { if (g_game) g_game->onPause(); }
JNI_FN(void, nativeOnResume)(JNIEnv*, jclass) { if (g_game) g_game->onResume(); }

JNI_FN(void, nativeOnDestroy)(JNIEnv*, jclass) {
    if (g_game) g_game->onDestroy();
    g_game.reset();
}

JNI_FN(void, nativeSetInput)(JNIEnv*, jclass, jfloat moveX, jfloat moveZ, jfloat lookDx, jfloat lookDy, jboolean jump,
                             jboolean sneak, jboolean breakPressed, jboolean placePressed, jboolean flyToggle,
                             jint hotbarSlot) {
    if (!g_game) return;
    pc::InputState in;
    in.moveX = moveX; in.moveZ = moveZ; in.lookDX = lookDx; in.lookDY = lookDy;
    in.jump = jump; in.sneak = sneak; in.breakPressed = breakPressed; in.placePressed = placePressed;
    in.flyToggle = flyToggle; in.hotbarSlot = hotbarSlot;
    g_game->setInput(in);
}

JNI_FN(void, nativeSetSetting)(JNIEnv*, jclass, jint key, jfloat value) {
    if (g_game) g_game->setSetting(key, value);
}

JNI_FN(jstring, nativeGetDebugInfo)(JNIEnv* env, jclass) {
    return env->NewStringUTF(g_game ? g_game->debugInfo().c_str() : "");
}
