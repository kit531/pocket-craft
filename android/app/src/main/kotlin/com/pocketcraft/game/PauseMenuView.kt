package com.pocketcraft.game

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import java.util.Locale
import kotlin.math.roundToInt

/**
 * Pause menu overlay: Resume, render distance, look sensitivity, FOV and a live debug line.
 * Values are saved and pushed to the engine by [GameSettings] as soon as they change.
 * There is no battery-saver (30 FPS) option because the engine has no setting for it yet.
 */
class PauseMenuView(
    context: Context,
    private val settings: GameSettings,
    onResumeGame: () -> Unit,
) : FrameLayout(context) {

    private val handler = Handler(Looper.getMainLooper())
    private val debugText = TextView(context)
    private val refreshDebug = object : Runnable {
        override fun run() {
            debugText.text = NativeBridge.nativeGetDebugInfo()
            handler.postDelayed(this, DEBUG_REFRESH_MS)
        }
    }

    init {
        val density = context.resources.displayMetrics.density
        fun dpInt(v: Int): Int = (v * density).toInt()

        setBackgroundColor(Color.argb(200, 0, 0, 0))
        isClickable = true  // swallow touches so they do not reach the game

        val column = LinearLayout(context)
        column.orientation = LinearLayout.VERTICAL
        column.setPadding(dpInt(24), dpInt(24), dpInt(24), dpInt(24))

        fun addRow(view: View, topGapDp: Int = 12) {
            column.addView(
                view,
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                ).apply { topMargin = dpInt(topGapDp) },
            )
        }

        fun label(): TextView = TextView(context).apply {
            setTextColor(Color.WHITE)
            textSize = 15f
        }

        val title = label().apply {
            text = "Paused"
            textSize = 24f
            gravity = Gravity.CENTER_HORIZONTAL
        }
        addRow(title, 0)

        val resume = Button(context).apply {
            text = "Resume"
            setOnClickListener { onResumeGame() }
        }
        addRow(resume, 16)

        // Render distance: slider 0..14 maps to 2..16 chunks.
        val rdLabel = label()
        val rdSeek = SeekBar(context)
        rdSeek.max = GameSettings.MAX_RENDER_DISTANCE - GameSettings.MIN_RENDER_DISTANCE
        rdSeek.progress = settings.renderDistance - GameSettings.MIN_RENDER_DISTANCE
        rdLabel.text = renderDistanceText(settings.renderDistance)
        rdSeek.onChange { progress ->
            val v = progress + GameSettings.MIN_RENDER_DISTANCE
            rdLabel.text = renderDistanceText(v)
            settings.renderDistance = v
        }
        addRow(rdLabel, 20)
        addRow(rdSeek, 4)

        // Look sensitivity: slider 0..100 maps linearly to 0.001..0.010 rad per pixel.
        val sensLabel = label()
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
        addRow(sensLabel, 20)
        addRow(sensSeek, 4)

        // Field of view: slider 0..60 maps to 50..110 degrees.
        val fovLabel = label()
        val fovSeek = SeekBar(context)
        fovSeek.max = (GameSettings.MAX_FOV - GameSettings.MIN_FOV).toInt()
        fovSeek.progress = (settings.fov - GameSettings.MIN_FOV).roundToInt()
        fovLabel.text = fovText(settings.fov)
        fovSeek.onChange { progress ->
            val v = GameSettings.MIN_FOV + progress
            fovLabel.text = fovText(v)
            settings.fov = v
        }
        addRow(fovLabel, 20)
        addRow(fovSeek, 4)

        debugText.setTextColor(Color.LTGRAY)
        debugText.textSize = 11f
        debugText.typeface = Typeface.MONOSPACE
        addRow(debugText, 24)

        val scroll = ScrollView(context)
        scroll.addView(
            column,
            FrameLayout.LayoutParams(dpInt(420), FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER_HORIZONTAL),
        )
        addView(scroll, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        visibility = View.GONE
    }

    /** Shows the menu and starts the 2 Hz debug refresh. */
    fun open() {
        visibility = View.VISIBLE
        handler.removeCallbacks(refreshDebug)
        handler.post(refreshDebug)
    }

    /** Hides the menu and stops the debug refresh. */
    fun close() {
        handler.removeCallbacks(refreshDebug)
        visibility = View.GONE
    }

    private fun renderDistanceText(v: Int): String = "Render distance: $v chunks"

    private fun sensitivityText(v: Float): String =
        String.format(Locale.US, "Look sensitivity: %.4f rad/px", v)

    private fun fovText(v: Float): String = "Field of view: ${v.roundToInt()} deg"

    private fun SeekBar.onChange(block: (Int) -> Unit) {
        setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                block(progress)
            }

            override fun onStartTrackingTouch(bar: SeekBar?) {}

            override fun onStopTrackingTouch(bar: SeekBar?) {}
        })
    }

    private companion object {
        const val DEBUG_REFRESH_MS = 500L
    }
}
