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
import android.widget.TextView

/**
 * Pause menu overlay: Resume, Back to home, settings and a live debug line.
 * Settings are saved and pushed to the engine by [GameSettings] as soon as they change.
 */
class PauseMenuView(
    context: Context,
    settings: GameSettings,
    onResumeGame: () -> Unit,
    onBackHome: () -> Unit,
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
        setBackgroundColor(Color.argb(200, 0, 0, 0))
        isClickable = true  // swallow touches so they do not reach the game

        val column = LinearLayout(context)
        column.orientation = LinearLayout.VERTICAL
        column.setPadding(context.dp(24), context.dp(24), context.dp(24), context.dp(24))

        fun addRow(view: View, topGapDp: Int) {
            column.addView(
                view,
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                ).apply { topMargin = context.dp(topGapDp) },
            )
        }

        val title = TextView(context).apply {
            text = "Paused / מושהה"
            setTextColor(Color.WHITE)
            textSize = 24f
            setTypeface(null, Typeface.BOLD)
            gravity = Gravity.CENTER_HORIZONTAL
        }
        addRow(title, 0)

        addRow(Button(context).apply {
            text = "Resume / המשך"
            setOnClickListener { onResumeGame() }
        }, 16)

        addRow(Button(context).apply {
            text = "Back to home / חזרה למסך הבית"
            setOnClickListener { onBackHome() }
        }, 8)

        addRow(SettingsPanel(context, settings, Color.WHITE, Color.LTGRAY), 20)

        debugText.setTextColor(Color.LTGRAY)
        debugText.textSize = 11f
        debugText.typeface = Typeface.MONOSPACE
        addRow(debugText, 24)

        val scroll = ScrollView(context)
        scroll.addView(
            column,
            FrameLayout.LayoutParams(context.dp(420), FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER_HORIZONTAL),
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

    private companion object {
        const val DEBUG_REFRESH_MS = 500L
    }
}
