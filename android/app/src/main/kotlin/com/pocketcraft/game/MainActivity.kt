package com.pocketcraft.game

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout

/**
 * The game screen, started by [HomeActivity] with a world name and seed. It runs in its own process
 * (":game", see AndroidManifest.xml): when the player leaves the game the process ends, so every world
 * launch starts a fresh engine.
 *
 * The SurfaceView is the game; touch controls, the inventory and the pause menu are overlaid on it.
 * The engine owns the render thread (see NativeBridge).
 */
class MainActivity : Activity(), SurfaceHolder.Callback {
    private lateinit var root: FrameLayout
    private lateinit var controls: ControlsView
    private lateinit var pauseMenu: PauseMenuView
    private var inventory: InventoryView? = null
    private var menuOpen = false

    /** True once nativeOnCreate ran. Native lifecycle calls are made only after this. */
    private var gameStarted = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val worldName: String = intent.getStringExtra(EXTRA_WORLD_NAME) ?: run {
            finish()
            return
        }
        val seed = intent.getLongExtra(EXTRA_SEED, 0L)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            // Use the notch area in landscape too.
            val lp = window.attributes
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            window.attributes = lp
        }

        NativeBridge.nativeOnCreate(filesDir.absolutePath, worldName, seed)
        gameStarted = true

        val settings = GameSettings(this)
        settings.applyToNative()

        val surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)

        controls = ControlsView(this).apply {
            onMenuPressed = { openPauseMenu() }
            onBagPressed = { openInventory() }
        }
        pauseMenu = PauseMenuView(
            this,
            settings,
            onResumeGame = { closePauseMenu() },
            onBackHome = { finish() },  // onPause saves the world, onDestroy tears the engine down
        )

        root = FrameLayout(this)
        root.addView(surfaceView, fillParams())
        root.addView(controls, fillParams())
        root.addView(pauseMenu, fillParams())
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        updateKeepScreenOn()
        if (gameStarted) NativeBridge.nativeOnResume()
    }

    override fun onPause() {
        if (gameStarted) {
            controls.releaseAll()
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            NativeBridge.nativeOnPause()  // saves the world
        }
        super.onPause()
    }

    override fun onDestroy() {
        if (gameStarted) {
            gameStarted = false
            NativeBridge.nativeOnDestroy()
        }
        super.onDestroy()
        // The player left the game: end the ":game" process so the next world starts from a fresh engine.
        if (isFinishing) Process.killProcess(Process.myPid())
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    /** Back closes the inventory or the pause menu, or opens the pause menu. */
    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        when {
            inventory != null -> closeInventory()
            menuOpen -> closePauseMenu()
            else -> openPauseMenu()
        }
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
        if (menuOpen || inventory != null) return
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

    private fun openInventory() {
        if (inventory != null || menuOpen) return
        controls.releaseAll()
        controls.visibility = View.GONE
        val view = InventoryView(
            context = this,
            initialSlot = controls.selectedHotbarSlot,
            slotBlocks = controls.slotBlockIds(),
            onPick = { slot, blockId ->
                NativeBridge.nativeSetHotbarSlot(slot, blockId)
                controls.setSlot(slot, blockId)
            },
            onSelectSlot = { slot -> controls.selectSlot(slot) },
            onClose = { closeInventory() },
        )
        root.addView(view, fillParams())
        inventory = view
    }

    private fun closeInventory() {
        val view = inventory ?: return
        inventory = null
        root.removeView(view)
        controls.visibility = View.VISIBLE
        controls.releaseAll()  // sends the chosen slot to the engine with a neutral snapshot
    }

    /** The screen stays on while playing, but not while the pause menu is up. */
    private fun updateKeepScreenOn() {
        if (menuOpen) {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun fillParams(): FrameLayout.LayoutParams = FrameLayout.LayoutParams(
        FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT,
    )

    companion object {
        const val EXTRA_WORLD_NAME = "com.pocketcraft.game.WORLD_NAME"
        const val EXTRA_SEED = "com.pocketcraft.game.SEED"
    }
}
