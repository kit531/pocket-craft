package com.pocketcraft.game

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.RectF

/**
 * The engine's procedural block atlas as an ARGB bitmap, plus helpers that draw one block's icon
 * (its side tile) with nearest-neighbour scaling so the pixels stay crisp.
 * UI thread only.
 */
object BlockAtlas {
    private const val ATLAS_PX = 256
    private const val TILES_PER_ROW = 16
    private const val TILE_PX = 16

    private val atlas: Bitmap? by lazy { decode() }
    private val paint = Paint()  // filter stays off: nearest neighbour
    private val src = Rect()
    private val tileCache = HashMap<Int, Int>()
    private val hebrewNames = mapOf(
        1 to "דשא", 2 to "עפר", 3 to "אבן", 4 to "חול", 5 to "מים", 6 to "בול עץ",
        7 to "עלווה", 8 to "לוח עץ", 9 to "זכוכית", 10 to "אבן מרוצפת", 11 to "סלע יסוד",
    )

    private fun decode(): Bitmap? {
        val rgba = NativeBridge.nativeGetAtlasRGBA()
        if (rgba.size != ATLAS_PX * ATLAS_PX * 4) return null
        val pixels = IntArray(ATLAS_PX * ATLAS_PX)
        for (i in pixels.indices) {
            val o = i * 4
            val r = rgba[o].toInt() and 0xFF
            val g = rgba[o + 1].toInt() and 0xFF
            val b = rgba[o + 2].toInt() and 0xFF
            val a = rgba[o + 3].toInt() and 0xFF
            pixels[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
        }
        return Bitmap.createBitmap(pixels, ATLAS_PX, ATLAS_PX, Bitmap.Config.ARGB_8888)
    }

    /** Ids of every block the player can place, in engine order. */
    val placeable: IntArray by lazy { NativeBridge.nativeGetPlaceableBlocks() }

    /** Draws block [blockId]'s icon into [dst]. Does nothing when the atlas is unavailable. */
    fun drawBlock(canvas: Canvas, blockId: Int, dst: RectF) {
        val bmp = atlas ?: return
        val tile = tileCache.getOrPut(blockId) { NativeBridge.nativeGetBlockIconTile(blockId) }
        val x = (tile % TILES_PER_ROW) * TILE_PX
        val y = (tile / TILES_PER_ROW) * TILE_PX
        src.set(x, y, x + TILE_PX, y + TILE_PX)
        canvas.drawBitmap(bmp, src, dst, paint)
    }

    /** Two-line label: Hebrew name, then the engine (English) name. */
    fun label(blockId: Int): String {
        val english = NativeBridge.nativeGetBlockName(blockId).replaceFirstChar { it.uppercaseChar() }
        val hebrew = hebrewNames[blockId] ?: return english
        return "$hebrew\n$english"
    }
}
