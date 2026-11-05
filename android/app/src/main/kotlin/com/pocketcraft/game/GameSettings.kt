package com.pocketcraft.game

import android.content.Context

/**
 * Player settings stored in SharedPreferences. Each setter also pushes the value to the engine.
 * The keys match the engine's `Setting` enum (engine/include/game.h).
 */
class GameSettings(context: Context) {
    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /** Chunk radius, 2..16. */
    var renderDistance: Int
        get() = prefs.getInt(KEY_RENDER_DISTANCE, DEFAULT_RENDER_DISTANCE).coerceIn(MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE)
        set(value) {
            val v = value.coerceIn(MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE)
            prefs.edit().putInt(KEY_RENDER_DISTANCE, v).apply()
            NativeBridge.nativeSetSetting(SETTING_RENDER_DISTANCE, v.toFloat())
        }

    /** Radians per touch pixel, 0.001..0.010. */
    var lookSensitivity: Float
        get() = prefs.getFloat(KEY_LOOK_SENSITIVITY, DEFAULT_LOOK_SENSITIVITY)
        set(value) {
            val v = value.coerceIn(MIN_LOOK_SENSITIVITY, MAX_LOOK_SENSITIVITY)
            prefs.edit().putFloat(KEY_LOOK_SENSITIVITY, v).apply()
            NativeBridge.nativeSetSetting(SETTING_LOOK_SENSITIVITY, v)
        }

    /** Vertical field of view in degrees, 50..110. */
    var fov: Float
        get() = prefs.getFloat(KEY_FOV, DEFAULT_FOV)
        set(value) {
            val v = value.coerceIn(MIN_FOV, MAX_FOV)
            prefs.edit().putFloat(KEY_FOV, v).apply()
            NativeBridge.nativeSetSetting(SETTING_FOV, v)
        }

    /** Pushes every saved value to the engine. Call after NativeBridge.nativeOnCreate. */
    fun applyToNative() {
        NativeBridge.nativeSetSetting(SETTING_RENDER_DISTANCE, renderDistance.toFloat())
        NativeBridge.nativeSetSetting(SETTING_LOOK_SENSITIVITY, lookSensitivity)
        NativeBridge.nativeSetSetting(SETTING_FOV, fov)
    }

    companion object {
        const val SETTING_RENDER_DISTANCE = 0
        const val SETTING_LOOK_SENSITIVITY = 1
        const val SETTING_FOV = 2

        const val MIN_RENDER_DISTANCE = 2
        const val MAX_RENDER_DISTANCE = 16
        const val DEFAULT_RENDER_DISTANCE = 8
        const val MIN_LOOK_SENSITIVITY = 0.001f
        const val MAX_LOOK_SENSITIVITY = 0.010f
        const val DEFAULT_LOOK_SENSITIVITY = 0.004f
        const val MIN_FOV = 50f
        const val MAX_FOV = 110f
        const val DEFAULT_FOV = 70f

        private const val PREFS_NAME = "pocketcraft_settings"
        private const val KEY_RENDER_DISTANCE = "render_distance"
        private const val KEY_LOOK_SENSITIVITY = "look_sensitivity"
        private const val KEY_FOV = "fov"
    }
}
