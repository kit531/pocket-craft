package com.pocketcraft.game

import android.view.Surface

/** JNI entry points, implemented in engine/platform/jni_bridge.cpp. See docs/ARCHITECTURE.md. */
object NativeBridge {
    init {
        System.loadLibrary("pocketcraft")
    }

    @JvmStatic external fun nativeOnCreate(filesDir: String, worldName: String, seed: Long)
    @JvmStatic external fun nativeOnSurfaceCreated(surface: Surface)
    @JvmStatic external fun nativeOnSurfaceChanged(width: Int, height: Int)
    @JvmStatic external fun nativeOnSurfaceDestroyed()
    @JvmStatic external fun nativeOnPause()
    @JvmStatic external fun nativeOnResume()
    @JvmStatic external fun nativeOnDestroy()
    @JvmStatic external fun nativeSetInput(
        moveX: Float, moveZ: Float, lookDx: Float, lookDy: Float,
        jump: Boolean, sneak: Boolean, breakPressed: Boolean, placePressed: Boolean,
        flyToggle: Boolean, hotbarSlot: Int,
    )
    @JvmStatic external fun nativeSetSetting(key: Int, value: Float)
    @JvmStatic external fun nativeGetDebugInfo(): String
}
