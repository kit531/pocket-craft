package com.pocketcraft.game

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.MotionEvent
import android.view.View
import kotlin.math.sqrt

/**
 * Touch overlay drawn on top of the game surface.
 *
 * One full-screen view handles every pointer (tracked by pointer id), so joystick, look drag and
 * buttons work at the same time.
 *  - Left 40 % of the screen: virtual joystick, centred where the touch lands.
 *  - Everywhere else: look drag. Short tap (< 200 ms, no real movement) = break, still for > 350 ms = place.
 *  - Round buttons on the right: Jump and Sneak (held), Break, Place and Fly (pulses), Menu.
 *  - Hotbar at the bottom centre; tap a slot to select it.
 *
 * Input goes to native as snapshots, at most one per frame (Choreographer). Pulses are sent as
 * true in one snapshot and false in the next, because Game::setInput acts on the false->true edge.
 */
class ControlsView(context: Context) : View(context) {

    /** Called on touch-down of the menu button. */
    var onMenuPressed: (() -> Unit)? = null

    private enum class Btn { JUMP, SNEAK, BREAK, PLACE, FLY, MENU }

    private class CircleButton(val kind: Btn, val label: String, val radiusDp: Float) {
        var cx = 0f
        var cy = 0f
        var r = 0f
    }

    /** A one-shot action: fire() once, then true for one snapshot and false for the next. */
    private class Pulse {
        private var pending = false
        private var high = false

        val active: Boolean
            get() = pending || high

        fun fire() {
            pending = true
        }

        fun reset() {
            pending = false
            high = false
        }

        /** The level to send in the next snapshot. */
        fun next(): Boolean {
            if (high) {
                high = false
                return false
            }
            if (pending) {
                pending = false
                high = true
                return true
            }
            return false
        }
    }

    private val density = context.resources.displayMetrics.density
    private fun dp(v: Float): Float = v * density

    private val slopPx = dp(10f)         // movement that turns a touch into a drag
    private val joyRadius = dp(56f)
    private val joyKnobRadius = dp(24f)

    private val buttons = listOf(
        CircleButton(Btn.JUMP, "JUMP", 36f),
        CircleButton(Btn.BREAK, "BREAK", 28f),
        CircleButton(Btn.PLACE, "PLACE", 28f),
        CircleButton(Btn.FLY, "FLY", 24f),
        CircleButton(Btn.SNEAK, "SNEAK", 24f),
        CircleButton(Btn.MENU, "MENU", 20f),
    )

    private val slotColors = intArrayOf(
        Color.rgb(93, 170, 58),   // grass
        Color.rgb(139, 90, 43),   // dirt
        Color.rgb(138, 138, 138), // stone
        Color.rgb(95, 95, 95),    // cobble
        Color.rgb(193, 154, 91),  // planks
        Color.rgb(107, 74, 36),   // log
        Color.rgb(227, 211, 154), // sand
        Color.rgb(184, 228, 240), // glass
        Color.rgb(47, 107, 42),   // leaves
    )
    private val slotRects = Array(9) { RectF() }
    private val hotbarBand = RectF()
    private var hotbarStep = 0f
    private var selectedSlot = 0

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }

    private val handler = Handler(Looper.getMainLooper())
    private val placeRunnable = Runnable {
        if (lookId != NO_POINTER && !lookMoved && !lookPlaced) {
            lookPlaced = true
            placePulse.fire()
            scheduleFrame()
        }
    }

    private val breakPulse = Pulse()
    private val placePulse = Pulse()
    private val flyPulse = Pulse()
    private var frameQueued = false
    private val frameCallback = Choreographer.FrameCallback { onFrame() }

    // Joystick state (normalized output, -1..1).
    private var joyId = NO_POINTER
    private var joyCx = 0f
    private var joyCy = 0f
    private var joyKnobX = 0f
    private var joyKnobY = 0f
    private var moveX = 0f
    private var moveZ = 0f

    // Look-drag state.
    private var lookId = NO_POINTER
    private var lookLastX = 0f
    private var lookLastY = 0f
    private var lookDownX = 0f
    private var lookDownY = 0f
    private var lookDownTime = 0L
    private var lookMoved = false
    private var lookPlaced = false
    private var lookAccX = 0f
    private var lookAccY = 0f

    // Held buttons, by pointer id.
    private val heldPointers = HashMap<Int, Btn>()
    private var jumpHeld = false
    private var sneakHeld = false

    /**
     * Drops every touch and sends a neutral snapshot right away. Call when the controls are hidden
     * (pause menu opened) or the activity pauses.
     */
    fun releaseAll() {
        clearTouchState()
        breakPulse.reset()
        placePulse.reset()
        flyPulse.reset()
        NativeBridge.nativeSetInput(0f, 0f, 0f, 0f, false, false, false, false, false, selectedSlot)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        layoutControls(w.toFloat(), h.toFloat())
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        Choreographer.getInstance().removeFrameCallback(frameCallback)
        frameQueued = false
        handler.removeCallbacks(placeRunnable)
    }

    private fun layoutControls(w: Float, h: Float) {
        for (b in buttons) {
            val pos = when (b.kind) {
                Btn.JUMP -> Pair(w - dp(72f), h - dp(72f))
                Btn.BREAK -> Pair(w - dp(160f), h - dp(72f))
                Btn.PLACE -> Pair(w - dp(72f), h - dp(160f))
                Btn.FLY -> Pair(w - dp(160f), h - dp(160f))
                Btn.SNEAK -> Pair(w - dp(72f), h - dp(248f))
                Btn.MENU -> Pair(w - dp(32f), dp(32f))
            }
            b.cx = pos.first
            b.cy = pos.second
            b.r = dp(b.radiusDp)
        }

        val size = dp(40f)
        val gap = dp(4f)
        hotbarStep = size + gap
        val total = 9 * size + 8 * gap
        val left = (w - total) / 2f
        val top = h - dp(12f) - size
        for (i in 0 until 9) {
            val x = left + i * hotbarStep
            slotRects[i].set(x, top, x + size, top + size)
        }
        hotbarBand.set(left - dp(6f), top - dp(6f), left + total + dp(6f), top + size + dp(6f))
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        drawHotbar(canvas)
        for (b in buttons) drawButton(canvas, b)
        if (joyId != NO_POINTER) drawJoystick(canvas)
    }

    private fun drawHotbar(canvas: Canvas) {
        fillPaint.color = Color.argb(90, 0, 0, 0)
        canvas.drawRoundRect(hotbarBand, dp(10f), dp(10f), fillPaint)
        for (i in 0 until 9) {
            val r = slotRects[i]
            fillPaint.color = slotColors[i]
            fillPaint.alpha = 230
            canvas.drawRoundRect(r, dp(4f), dp(4f), fillPaint)
            val selected = i == selectedSlot
            strokePaint.strokeWidth = if (selected) dp(3f) else dp(1f)
            strokePaint.color = if (selected) Color.WHITE else Color.argb(140, 255, 255, 255)
            canvas.drawRoundRect(r, dp(4f), dp(4f), strokePaint)
        }
    }

    private fun drawButton(canvas: Canvas, b: CircleButton) {
        val pressed = heldPointers.containsValue(b.kind)
        fillPaint.color = Color.argb(if (pressed) 170 else 80, 0, 0, 0)
        canvas.drawCircle(b.cx, b.cy, b.r, fillPaint)
        strokePaint.strokeWidth = dp(2f)
        strokePaint.color = Color.argb(170, 255, 255, 255)
        canvas.drawCircle(b.cx, b.cy, b.r, strokePaint)
        textPaint.textSize = dp(if (b.kind == Btn.MENU) 9f else 10f)
        val textY = b.cy - (textPaint.descent() + textPaint.ascent()) / 2f
        canvas.drawText(b.label, b.cx, textY, textPaint)
    }

    private fun drawJoystick(canvas: Canvas) {
        strokePaint.strokeWidth = dp(2f)
        strokePaint.color = Color.argb(120, 255, 255, 255)
        canvas.drawCircle(joyCx, joyCy, joyRadius, strokePaint)
        fillPaint.color = Color.argb(120, 255, 255, 255)
        canvas.drawCircle(joyKnobX, joyKnobY, joyKnobRadius, fillPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                onPointerDown(event.getPointerId(i), event.getX(i), event.getY(i))
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    onPointerMove(event.getPointerId(i), event.getX(i), event.getY(i))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> onPointerUp(event.getPointerId(event.actionIndex))
            MotionEvent.ACTION_CANCEL -> {
                clearTouchState()
                scheduleFrame()
            }
        }
        return true
    }

    private fun onPointerDown(id: Int, x: Float, y: Float) {
        val slot = hotbarIndexAt(x, y)
        if (slot >= 0) {
            selectedSlot = slot
            invalidate()
            scheduleFrame()
            return
        }

        val button = buttonAt(x, y)
        if (button != null) {
            if (button.kind == Btn.MENU) {
                onMenuPressed?.invoke()
                return
            }
            heldPointers[id] = button.kind
            when (button.kind) {
                Btn.BREAK -> breakPulse.fire()
                Btn.PLACE -> placePulse.fire()
                Btn.FLY -> flyPulse.fire()
                else -> Unit  // Jump and Sneak are held levels, see updateHeldLevels
            }
            updateHeldLevels()
            invalidate()
            scheduleFrame()
            return
        }

        if (x < width * JOY_ZONE && joyId == NO_POINTER) {
            joyId = id
            joyCx = x
            joyCy = y
            updateJoystick(x, y)
            invalidate()
            scheduleFrame()
            return
        }

        if (lookId == NO_POINTER) {
            lookId = id
            lookLastX = x
            lookLastY = y
            lookDownX = x
            lookDownY = y
            lookDownTime = SystemClock.uptimeMillis()
            lookMoved = false
            lookPlaced = false
            handler.postDelayed(placeRunnable, LONG_PRESS_MS)
        }
    }

    private fun onPointerMove(id: Int, x: Float, y: Float) {
        if (id == joyId) {
            updateJoystick(x, y)
            invalidate()
            scheduleFrame()
        } else if (id == lookId) {
            lookAccX += x - lookLastX
            lookAccY += y - lookLastY
            lookLastX = x
            lookLastY = y
            if (!lookMoved && lengthOf(x - lookDownX, y - lookDownY) > slopPx) {
                lookMoved = true
                handler.removeCallbacks(placeRunnable)
            }
            scheduleFrame()
        }
    }

    private fun onPointerUp(id: Int) {
        if (id == joyId) {
            joyId = NO_POINTER
            moveX = 0f
            moveZ = 0f
            invalidate()
            scheduleFrame()
        }
        if (heldPointers.remove(id) != null) {
            updateHeldLevels()
            invalidate()
            scheduleFrame()
        }
        if (id == lookId) {
            handler.removeCallbacks(placeRunnable)
            val quick = SystemClock.uptimeMillis() - lookDownTime < TAP_MS
            if (quick && !lookMoved && !lookPlaced) breakPulse.fire()
            lookId = NO_POINTER
            scheduleFrame()
        }
    }

    /** Joystick: dead zone, then rescale so output still goes 0..1 at the rim. Up on screen = forward. */
    private fun updateJoystick(x: Float, y: Float) {
        var nx = (x - joyCx) / joyRadius
        var ny = (y - joyCy) / joyRadius
        val len = lengthOf(nx, ny)
        if (len > 1f) {
            nx /= len
            ny /= len
        }
        val mag = if (len > 1f) 1f else len
        joyKnobX = joyCx + nx * joyRadius
        joyKnobY = joyCy + ny * joyRadius
        if (mag < JOY_DEAD_ZONE) {
            moveX = 0f
            moveZ = 0f
        } else {
            val scale = (mag - JOY_DEAD_ZONE) / (1f - JOY_DEAD_ZONE) / mag
            moveX = nx * scale
            moveZ = -ny * scale
        }
    }

    private fun updateHeldLevels() {
        jumpHeld = heldPointers.containsValue(Btn.JUMP)
        sneakHeld = heldPointers.containsValue(Btn.SNEAK)
    }

    /** Forgets all touches and held levels. Pulses are left alone so a pending action is not lost. */
    private fun clearTouchState() {
        handler.removeCallbacks(placeRunnable)
        joyId = NO_POINTER
        moveX = 0f
        moveZ = 0f
        lookId = NO_POINTER
        lookAccX = 0f
        lookAccY = 0f
        heldPointers.clear()
        updateHeldLevels()
        invalidate()
    }

    private fun hotbarIndexAt(x: Float, y: Float): Int {
        if (!hotbarBand.contains(x, y)) return -1
        return ((x - slotRects[0].left) / hotbarStep).toInt().coerceIn(0, 8)
    }

    private fun buttonAt(x: Float, y: Float): CircleButton? = buttons.firstOrNull { b ->
        lengthOf(x - b.cx, y - b.cy) <= b.r + slopPx
    }

    private fun scheduleFrame() {
        if (frameQueued) return
        frameQueued = true
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    private fun onFrame() {
        frameQueued = false
        sendSnapshot()
        if (breakPulse.active || placePulse.active || flyPulse.active) scheduleFrame()
    }

    private fun sendSnapshot() {
        val breakOut = breakPulse.next()
        val placeOut = placePulse.next()
        val flyOut = flyPulse.next()
        val dx = lookAccX
        val dy = lookAccY
        lookAccX = 0f
        lookAccY = 0f
        NativeBridge.nativeSetInput(moveX, moveZ, dx, dy, jumpHeld, sneakHeld, breakOut, placeOut, flyOut, selectedSlot)
    }

    private companion object {
        const val NO_POINTER = -1
        const val LONG_PRESS_MS = 350L
        const val TAP_MS = 200L
        const val JOY_ZONE = 0.4f
        const val JOY_DEAD_ZONE = 0.15f
    }
}

private fun lengthOf(x: Float, y: Float): Float = sqrt(x * x + y * y)
