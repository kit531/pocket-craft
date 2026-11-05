package com.pocketcraft.game

import android.app.Activity
import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager

class MainActivity : Activity(), SurfaceHolder.Callback {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        NativeBridge.nativeOnCreate(filesDir.absolutePath, "world1", 12345L)
        val view = SurfaceView(this)
        view.holder.addCallback(this)
        setContentView(view)
        // Touch controls (joystick, jump, break, place, hotbar) are overlaid here by the android bot.
    }

    override fun onResume() {
        super.onResume()
        NativeBridge.nativeOnResume()
    }

    override fun onPause() {
        NativeBridge.nativeOnPause()
        super.onPause()
    }

    override fun onDestroy() {
        NativeBridge.nativeOnDestroy()
        super.onDestroy()
    }

    override fun surfaceCreated(holder: SurfaceHolder) = NativeBridge.nativeOnSurfaceCreated(holder.surface)

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) =
        NativeBridge.nativeOnSurfaceChanged(width, height)

    override fun surfaceDestroyed(holder: SurfaceHolder) = NativeBridge.nativeOnSurfaceDestroyed()
}
