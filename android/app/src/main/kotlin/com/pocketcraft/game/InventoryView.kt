package com.pocketcraft.game

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.FrameLayout
import android.widget.GridView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * Full-screen inventory over the game. A row of the 9 hotbar slots (tap one to select it) and a grid
 * of every placeable block. Tapping a block puts it in the selected slot. The game keeps running
 * behind the overlay, like the pause menu.
 *
 * [slotBlocks] is a copy the view updates locally; the owner also sends the change to the engine
 * through [onPick].
 */
class InventoryView(
    context: Context,
    initialSlot: Int,
    slotBlocks: IntArray,
    private val onPick: (slot: Int, blockId: Int) -> Unit,
    private val onSelectSlot: (slot: Int) -> Unit,
    private val onClose: () -> Unit,
) : FrameLayout(context) {

    private val blocks = BlockAtlas.placeable
    private val slotBlocks = slotBlocks.copyOf()
    private var selected = initialSlot.coerceIn(0, 8)
    private val slotViews = ArrayList<BlockIconView>(9)
    private val hint: TextView

    init {
        setBackgroundColor(Color.argb(170, 0, 0, 0))
        isClickable = true  // swallow touches so they do not reach the game

        val panel = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            background = roundedBox(Color.argb(240, 22, 36, 18), context.dpF(20f))
            setPadding(context.dp(16), context.dp(12), context.dp(16), context.dp(12))
        }

        // Header: title and close.
        val header = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(
            TextView(context).apply {
                text = "Inventory / מלאי"
                setTextColor(Color.WHITE)
                textSize = 20f
                setTypeface(null, Typeface.BOLD)
            },
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f),
        )
        header.addView(
            TextView(context).apply {
                text = "Close / סגור"
                setTextColor(Color.WHITE)
                textSize = 16f
                setTypeface(null, Typeface.BOLD)
                gravity = Gravity.CENTER
                setPadding(context.dp(18), context.dp(10), context.dp(18), context.dp(10))
                background = rippled(roundedBox(Color.argb(90, 255, 255, 255), context.dpF(22f)), Color.argb(80, 255, 255, 255))
                isClickable = true
                setOnClickListener { onClose() }
            },
        )
        panel.addView(header)

        // Hotbar row.
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }
        for (i in 0 until 9) {
            val slot = BlockIconView(context).apply {
                blockId = slotBlocks[i]
                highlighted = i == selected
                setOnClickListener { selectSlot(i) }
            }
            slotViews.add(slot)
            row.addView(
                slot,
                LinearLayout.LayoutParams(0, context.dp(56), 1f).apply {
                    setMargins(context.dp(3), context.dp(8), context.dp(3), context.dp(4))
                },
            )
        }
        panel.addView(row)

        hint = TextView(context).apply {
            setTextColor(Color.argb(230, 230, 245, 220))
            textSize = 14f
            gravity = Gravity.CENTER
            text = hintText(selected)
        }
        panel.addView(hint, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        // Every placeable block.
        val grid = GridView(context).apply {
            numColumns = GridView.AUTO_FIT
            columnWidth = context.dp(88)
            stretchMode = GridView.STRETCH_COLUMN_WIDTH
            horizontalSpacing = context.dp(8)
            verticalSpacing = context.dp(8)
            adapter = BlockAdapter()
            setOnItemClickListener { _, _, position, _ -> pick(blocks[position]) }
        }
        panel.addView(grid, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f).apply {
            topMargin = context.dp(8)
        })

        addView(
            panel,
            LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT).apply {
                val m = context.dp(24)
                setMargins(m, m, m, m)
            },
        )
    }

    private fun hintText(slot: Int): String =
        "Tap a block to put it in slot ${slot + 1} / לחצו על בלוק כדי לשים אותו בחריץ ${slot + 1}"

    private fun selectSlot(slot: Int) {
        selected = slot
        for (i in slotViews.indices) slotViews[i].highlighted = i == slot
        hint.text = hintText(slot)
        onSelectSlot(slot)
    }

    private fun pick(blockId: Int) {
        slotBlocks[selected] = blockId
        slotViews[selected].blockId = blockId
        onPick(selected, blockId)
    }

    private inner class BlockAdapter : BaseAdapter() {
        override fun getCount(): Int = blocks.size

        override fun getItem(position: Int): Any = blocks[position]

        override fun getItemId(position: Int): Long = blocks[position].toLong()

        override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
            val cell = (convertView as? LinearLayout) ?: createCell()
            val icon = cell.getChildAt(0) as BlockIconView
            val label = cell.getChildAt(1) as TextView
            icon.blockId = blocks[position]
            label.text = BlockAtlas.label(blocks[position])
            return cell
        }

        private fun createCell(): LinearLayout {
            val ctx = context
            return LinearLayout(ctx).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_HORIZONTAL
                setPadding(ctx.dp(6), ctx.dp(8), ctx.dp(6), ctx.dp(8))
                background = roundedBox(Color.argb(40, 255, 255, 255), ctx.dpF(12f))
                addView(BlockIconView(ctx), LinearLayout.LayoutParams(ctx.dp(56), ctx.dp(56)))
                addView(
                    TextView(ctx).apply {
                        setTextColor(Color.WHITE)
                        textSize = 12f
                        gravity = Gravity.CENTER
                        maxLines = 2
                    },
                    LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT),
                )
            }
        }
    }
}
