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

    // Block / hotbar / atlas access. The getters work before nativeOnCreate (no Game needed).
    @JvmStatic external fun nativeSetHotbarSlot(slot: Int, blockId: Int)
    @JvmStatic external fun nativeGetHotbarSlot(slot: Int): Int
    @JvmStatic external fun nativeGetPlaceableBlocks(): IntArray
    @JvmStatic external fun nativeGetBlockName(id: Int): String
    @JvmStatic external fun nativeGetBlockIconTile(id: Int): Int
    /** Level-0 atlas, 256x256 RGBA8, 262144 bytes. */
    @JvmStatic external fun nativeGetAtlasRGBA(): ByteArray
}
