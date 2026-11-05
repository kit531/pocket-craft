package com.pocketcraft.game

import android.content.Context
import android.view.Gravity
import android.view.View
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import java.util.Locale
import kotlin.math.roundToInt

/**
 * Render distance, look sensitivity, field of view and battery saver. Used by the home screen
 * and the pause menu. Every change is saved and pushed to the engine by [GameSettings].
 */
class SettingsPanel(
    context: Context,
    private val settings: GameSettings,
    textColor: Int,
    subColor: Int,
) : LinearLayout(context) {

    init {
        orientation = VERTICAL

        fun labelView(): TextView = TextView(context).apply {
            setTextColor(textColor)
            textSize = 15f
        }

        fun addRow(view: View, topGapDp: Int) {
            addView(
                view,
                LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT).apply {
                    topMargin = context.dp(topGapDp)
                },
            )
        }

        // Render distance: slider 0..14 maps to 2..16 chunks.
        val rdLabel = labelView()
        val rdSeek = SeekBar(context)
        rdSeek.max = GameSettings.MAX_RENDER_DISTANCE - GameSettings.MIN_RENDER_DISTANCE
        rdSeek.progress = settings.renderDistance - GameSettings.MIN_RENDER_DISTANCE
        rdLabel.text = renderDistanceText(settings.renderDistance)
        rdSeek.onChange { progress ->
            val v = progress + GameSettings.MIN_RENDER_DISTANCE
            rdLabel.text = renderDistanceText(v)
            settings.renderDistance = v
        }
        addRow(rdLabel, 12)
        addRow(rdSeek, 4)

        // Look sensitivity: slider 0..100 maps linearly to 0.001..0.010 rad per pixel.
        val sensLabel = labelView()
        val sensSeek = SeekBar(context)
        sensSeek.max = 100
        sensSeek.progress = ((settings.lookSensitivity - GameSettings.MIN_LOOK_SENSITIVITY) /
            (GameSettings.MAX_LOOK_SENSITIVITY - GameSettings.MIN_LOOK_SENSITIVITY) * 100f).roundToInt()
        sensLabel.text = sensitivityText(settings.lookSensitivity)
        sensSeek.onChange { progress ->
            val v = GameSettings.MIN_LOOK_SENSITIVITY +
                progress / 100f * (GameSettings.MAX_LOOK_SENSITIVITY - GameSettings.MIN_LOOK_SENSITIVITY)
            sensLabel.text = sensitivityText(v)
            settings.lookSensitivity = v
        }
        addRow(sensLabel, 12)
        addRow(sensSeek, 4)

        // Field of view: slider 0..60 maps to 50..110 degrees.
        val fovLabel = labelView()
        val fovSeek = SeekBar(context)
        fovSeek.max = (GameSettings.MAX_FOV - GameSettings.MIN_FOV).toInt()
        fovSeek.progress = (settings.fov - GameSettings.MIN_FOV).roundToInt()
        fovLabel.text = fovText(settings.fov)
        fovSeek.onChange { progress ->
            val v = GameSettings.MIN_FOV + progress
            fovLabel.text = fovText(v)
            settings.fov = v
        }
        addRow(fovLabel, 12)
        addRow(fovSeek, 4)

        val saver = CheckBox(context).apply {
            text = "Battery saver (30 FPS) / חיסכון בסוללה (30 FPS)"
            setTextColor(textColor)
            isChecked = settings.batterySaver
            setOnCheckedChangeListener { _, checked -> settings.batterySaver = checked }
        }
        addRow(saver, 12)

        // Hint line under the sliders.
        val hint = TextView(context).apply {
            text = "Changes apply the next time you play. / השינויים יחולו בפעם הבאה שתשחקו."
            setTextColor(subColor)
            textSize = 12f
            gravity = Gravity.START
        }
        addRow(hint, 14)
    }

    private fun renderDistanceText(v: Int): String = "Render distance: $v chunks / מרחק הצגה: $v"

    private fun sensitivityText(v: Float): String =
        String.format(Locale.US, "Look sensitivity: %.4f rad/px / רגישות מבט", v)

    private fun fovText(v: Float): String = "Field of view: ${v.roundToInt()} deg / שדה ראייה"

    private fun SeekBar.onChange(block: (Int) -> Unit) {
        setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                block(progress)
            }

            override fun onStartTrackingTouch(bar: SeekBar?) {}

            override fun onStopTrackingTouch(bar: SeekBar?) {}
        })
    }
}
