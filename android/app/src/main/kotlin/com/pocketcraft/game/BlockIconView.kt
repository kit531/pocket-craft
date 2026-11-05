package com.pocketcraft.game

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.view.View
import kotlin.math.min

/** A square tile with one block's icon from the atlas. */
class BlockIconView(context: Context) : View(context) {
    var blockId: Int = 0
        set(value) {
            field = value
            invalidate()
        }

    var highlighted: Boolean = false
        set(value) {
            field = value
            invalidate()
        }

    private val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        color = Color.WHITE
    }
    private val frame = RectF()
    private val icon = RectF()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val side = min(width, height).toFloat()
        val radius = side * 0.18f
        frame.set(0f, 0f, width.toFloat(), height.toFloat())
        bgPaint.style = Paint.Style.FILL
        bgPaint.color = Color.argb(if (highlighted) 110 else 50, 255, 255, 255)
        canvas.drawRoundRect(frame, radius, radius, bgPaint)
        if (highlighted) {
            strokePaint.strokeWidth = context.dpF(3f)
            canvas.drawRoundRect(frame, radius, radius, strokePaint)
        }
        val pad = side * 0.14f
        icon.set(pad, pad, width - pad, height - pad)
        BlockAtlas.drawBlock(canvas, blockId, icon)
    }
}
