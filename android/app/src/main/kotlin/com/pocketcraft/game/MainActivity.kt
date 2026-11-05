package com.pocketcraft.game

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout

/**
 * Fullscreen landscape game activity. The SurfaceView is the game; touch controls and the pause
 * menu are overlaid on it in a FrameLayout. The engine owns the render thread (see NativeBridge).
 */
class MainActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var settings: GameSettings
    private lateinit var controls: ControlsView
    private lateinit var pauseMenu: PauseMenuView
    private var menuOpen = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            // Use the notch area in landscape too.
            val lp = window.attributes
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            window.attributes = lp
        }

        NativeBridge.nativeOnCreate(filesDir.absolutePath, WORLD_NAME, WORLD_SEED)
        settings = GameSettings(this)
        settings.applyToNative()

        val surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)

        controls = ControlsView(this)
        controls.onMenuPressed = { openPauseMenu() }

        pauseMenu = PauseMenuView(this, settings) { closePauseMenu() }

        val root = FrameLayout(this)
        root.addView(surfaceView, fillParams())
        root.addView(controls, fillParams())
        root.addView(pauseMenu, fillParams())
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        updateKeepScreenOn()
        NativeBridge.nativeOnResume()
    }

    override fun onPause() {
        controls.releaseAll()
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        NativeBridge.nativeOnPause()
        super.onPause()
    }

    override fun onDestroy() {
        NativeBridge.nativeOnDestroy()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    /** Back opens the pause menu; back again closes it. */
    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (menuOpen) closePauseMenu() else openPauseMenu()
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        NativeBridge.nativeOnSurfaceCreated(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        NativeBridge.nativeOnSurfaceChanged(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        NativeBridge.nativeOnSurfaceDestroyed()
    }

    private fun openPauseMenu() {
        if (menuOpen) return
        menuOpen = true
        controls.releaseAll()
        controls.visibility = View.GONE
        pauseMenu.open()
        updateKeepScreenOn()
    }

    private fun closePauseMenu() {
        if (!menuOpen) return
        menuOpen = false
        pauseMenu.close()
        controls.visibility = View.VISIBLE
        updateKeepScreenOn()
    }

    /** The screen stays on only while playing, not while the pause menu is up. */
    private fun updateKeepScreenOn() {
        if (menuOpen) {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    /** Fullscreen immersive: hides status and navigation bars; they come back on a swipe. */
    @Suppress("DEPRECATION")
    private fun hideSystemBars() {
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

    private fun fillParams(): FrameLayout.LayoutParams = FrameLayout.LayoutParams(
        FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT,
    )

    companion object {
        private const val WORLD_NAME = "world1"
        private const val WORLD_SEED = 12345L
    }
}
