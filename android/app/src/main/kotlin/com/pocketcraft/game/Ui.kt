package com.pocketcraft.game

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.graphics.drawable.Drawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController

/** Green palette (Pocket Client look). Light and dark follow the system setting. */
class Palette private constructor(dark: Boolean) {
    val primary: Int = if (dark) color("#8FD866") else color("#2F6E12")
    val onPrimary: Int = if (dark) color("#13300A") else color("#FFFFFF")
    val primaryContainer: Int = if (dark) color("#3E7A1F") else color("#B8EE96")
    val onPrimaryContainer: Int = if (dark) color("#E8FFD9") else color("#0E2A04")
    val background: Int = if (dark) color("#0F1E0D") else color("#EAF5E1")
    val surface: Int = if (dark) color("#172B14") else color("#F6FBF0")
    val onSurface: Int = if (dark) color("#E3F2D8") else color("#1A2E14")
    val onSurfaceVariant: Int = if (dark) color("#A9C798") else color("#4B6B3C")
    val error: Int = color("#B3261E")
    val scrim: Int = color("#A0000000")

    companion object {
        fun from(context: Context): Palette {
            val night = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
            return Palette(night == Configuration.UI_MODE_NIGHT_YES)
        }

        private fun color(hex: String): Int = android.graphics.Color.parseColor(hex)
    }
}

fun Context.dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()

fun Context.dpF(v: Float): Float = v * resources.displayMetrics.density

fun roundedBox(color: Int, radiusPx: Float): GradientDrawable = GradientDrawable().apply {
    shape = GradientDrawable.RECTANGLE
    cornerRadius = radiusPx
    setColor(color)
}

/** Wraps a drawable with a ripple, so rounded buttons show touch feedback. */
fun rippled(content: Drawable, rippleColor: Int): RippleDrawable =
    RippleDrawable(ColorStateList.valueOf(rippleColor), content, null)

/** Fullscreen immersive: hides status and navigation bars; they come back on a swipe. */
@Suppress("DEPRECATION")
fun Activity.hideSystemBars() {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
        window.setDecorFitsSystemWindows(false)
        val controller = window.insetsController
        if (controller != null) {
            controller.hide(WindowInsets.Type.systemBars())
            controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    } else {
        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
    }
}
