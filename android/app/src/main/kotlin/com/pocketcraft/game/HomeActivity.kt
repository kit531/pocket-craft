package com.pocketcraft.game

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.text.DateFormat
import java.util.Date

/**
 * Launcher screen: the Pocket Craft home. Play (last world), the world list, New world, Delete and
 * Settings. Shown in landscape; follows the system light/dark setting.
 */
class HomeActivity : Activity() {
    private lateinit var palette: Palette
    private lateinit var store: WorldStore
    private lateinit var root: FrameLayout
    private lateinit var worldList: LinearLayout
    private lateinit var playSub: TextView
    private lateinit var settings: GameSettings
    private var overlay: View? = null
    private var ready = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        palette = Palette.from(this)
        store = WorldStore(this)
        settings = GameSettings(this)
        window.setBackgroundDrawable(ColorDrawable(palette.background))
        root = FrameLayout(this)
        setContentView(root)

        if (LegacyDataCleaner.isDone(this)) {
            buildUi()
        } else {
            // First start after the update: delete the old launcher's data off the UI thread, then show the home.
            root.addView(TextView(this).apply {
                text = PREPARING_TEXT
                gravity = Gravity.CENTER
                setTextColor(palette.onSurface)
                textSize = 18f
            }, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
            Thread {
                LegacyDataCleaner.cleanIfNeeded(applicationContext)
                runOnUiThread {
                    if (!isDestroyed && !isFinishing) buildUi()
                }
            }.start()
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        if (ready) refresh()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (overlay != null) hideOverlay() else super.onBackPressed()
    }

    private fun buildUi() {
        ready = true
        root.removeAllViews()
        overlay = null

        val column = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(14), dp(24), dp(16))
        }
        column.addView(topBar(), LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        val content = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        content.addView(playPanel(), LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply {
            rightMargin = dp(20)
        })
        content.addView(worldsPanel(), LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1.5f))
        column.addView(content, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f).apply {
            topMargin = dp(14)
        })

        root.addView(column, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        refresh()
    }

    private fun refresh() {
        renderWorlds()
        val target = targetWorld()
        playSub.text = if (target != null) {
            "World: $target / עולם: $target"
        } else {
            "Create a world to start / צרו עולם כדי להתחיל"
        }
    }

    /** Logo, title and Settings. */
    private fun topBar(): LinearLayout {
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        bar.addView(ImageView(this).apply {
            setImageResource(R.drawable.ic_launcher_foreground)
            scaleType = ImageView.ScaleType.FIT_CENTER
        }, LinearLayout.LayoutParams(dp(56), dp(56)))

        val titles = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(10), 0, 0, 0)
        }
        titles.addView(TextView(this).apply {
            text = "Pocket Craft"
            setTextColor(palette.primary)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 28f)
            setTypeface(null, Typeface.BOLD)
        })
        titles.addView(TextView(this).apply {
            text = "פוקט קראפט"
            setTextColor(palette.onSurfaceVariant)
            textSize = 15f
        })
        bar.addView(titles, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))

        bar.addView(pill("Settings / הגדרות", Kind.TONAL) { showSettings() })
        return bar
    }

    /** Big Play button: opens the last played world. */
    private fun playPanel(): LinearLayout {
        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
        }
        val button = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(16), dp(16), dp(16))
            background = rippled(roundedBox(palette.primary, dpF(28f)), Color.argb(60, 255, 255, 255))
            isClickable = true
            isFocusable = true
            setOnClickListener { onPlay() }
        }
        button.addView(TextView(this).apply {
            text = "שחק / Play"
            setTextColor(palette.onPrimary)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 34f)
            setTypeface(null, Typeface.BOLD)
            gravity = Gravity.CENTER
        })
        playSub = TextView(this).apply {
            setTextColor(palette.onPrimary)
            textSize = 15f
            gravity = Gravity.CENTER
            setPadding(0, dp(6), 0, 0)
        }
        button.addView(playSub)
        panel.addView(button, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.MATCH_PARENT))
        return panel
    }

    /** Header with New world, then the scrollable list of world cards. */
    private fun worldsPanel(): LinearLayout {
        val panel = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        header.addView(TextView(this).apply {
            text = "Worlds / עולמות"
            setTextColor(palette.onSurface)
            textSize = 20f
            setTypeface(null, Typeface.BOLD)
        }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        header.addView(pill("+ New world / עולם חדש", Kind.FILLED) { showNewWorld() })
        panel.addView(header, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        val scroll = ScrollView(this)
        worldList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        scroll.addView(worldList, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT))
        panel.addView(scroll, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f).apply {
            topMargin = dp(10)
        })
        return panel
    }

    private fun renderWorlds() {
        worldList.removeAllViews()
        val worlds = store.list()
        if (worlds.isEmpty()) {
            worldList.addView(TextView(this).apply {
                text = "No worlds yet. Tap New world. / אין עדיין עולמות. לחצו על עולם חדש."
                setTextColor(palette.onSurfaceVariant)
                textSize = 15f
                setPadding(dp(4), dp(8), dp(4), dp(8))
            })
            return
        }
        val dateFormat = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT)
        for (info in worlds) {
            val played = if (info.lastPlayed > 0L) {
                "Last played / נפתח לאחרונה: ${dateFormat.format(Date(info.lastPlayed))}"
            } else {
                "Not played yet / עדיין לא נפתח"
            }
            worldList.addView(
                worldCard(info.name, played),
                LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                    bottomMargin = dp(10)
                },
            )
        }
    }

    private fun worldCard(name: String, playedText: String): LinearLayout {
        return LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(12))
            background = rippled(roundedBox(palette.surface, dpF(16f)), Color.argb(50, Color.red(palette.primary), Color.green(palette.primary), Color.blue(palette.primary)))
            isClickable = true
            isFocusable = true
            setOnClickListener { playWorld(name) }
            setOnLongClickListener {
                confirmDelete(name)
                true
            }
            addView(TextView(context).apply {
                text = name
                setTextColor(palette.onSurface)
                textSize = 18f
                setTypeface(null, Typeface.BOLD)
            })
            addView(TextView(context).apply {
                text = playedText
                setTextColor(palette.onSurfaceVariant)
                textSize = 13f
            })
        }
    }

    /** The world Play opens: the last played one, else the newest one. */
    private fun targetWorld(): String? = store.lastWorld() ?: store.list().firstOrNull()?.name

    private fun onPlay() {
        val target = targetWorld()
        if (target == null) showNewWorld() else playWorld(target)
    }

    private fun playWorld(name: String) {
        store.markPlayed(name)
        startActivity(
            Intent(this, MainActivity::class.java)
                .putExtra(MainActivity.EXTRA_WORLD_NAME, name)
                .putExtra(MainActivity.EXTRA_SEED, store.seedOf(name)),
        )
    }

    private fun showNewWorld() {
        val nameInput = EditText(this).apply {
            hint = "Name / שם"
            setSingleLine()
            setTextColor(palette.onSurface)
            setHintTextColor(palette.onSurfaceVariant)
        }
        val seedInput = EditText(this).apply {
            hint = "Seed number (optional) / מספר זרע (לא חובה)"
            setSingleLine()
            inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED
            setTextColor(palette.onSurface)
            setHintTextColor(palette.onSurfaceVariant)
        }
        val error = TextView(this).apply {
            setTextColor(palette.error)
            textSize = 14f
        }
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(nameInput)
            addView(seedInput, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                topMargin = dp(8)
            })
            addView(error)
        }
        showCard(
            title = "New world / עולם חדש",
            body = body,
            buttons = listOf(
                pill("Cancel / ביטול", Kind.TONAL) { hideOverlay() },
                pill("Create / צור", Kind.FILLED) {
                    val name = nameInput.text.toString().trim()
                    val seedText = seedInput.text.toString().trim()
                    val seed = if (seedText.isEmpty()) WorldStore.randomSeed() else seedText.toLongOrNull()
                    val nameError = store.checkName(name)
                    when {
                        nameError != null -> error.text = nameError
                        seed == null -> error.text = "Seed must be a number / הזרע חייב להיות מספר"
                        else -> {
                            store.create(name, seed)
                            hideOverlay()
                            refresh()
                        }
                    }
                },
            ),
        )
    }

    private fun confirmDelete(name: String) {
        showCard(
            title = "Delete world? / למחוק את העולם?",
            body = TextView(this).apply {
                text = "\"$name\" and everything built in it will be deleted. This cannot be undone.\n" +
                    "העולם \"$name\" וכל מה שנבנה בו יימחקו. אי אפשר לבטל."
                setTextColor(palette.onSurface)
                textSize = 15f
            },
            buttons = listOf(
                pill("Cancel / ביטול", Kind.TONAL) { hideOverlay() },
                pill("Delete / מחק", Kind.DANGER) {
                    store.delete(name)
                    hideOverlay()
                    refresh()
                },
            ),
        )
    }

    private fun showSettings() {
        showCard(
            title = "Settings / הגדרות",
            body = SettingsPanel(this, settings, palette.onSurface, palette.onSurfaceVariant),
            buttons = listOf(pill("Close / סגור", Kind.FILLED) { hideOverlay() }),
            scrollBody = true,
        )
    }

    /** A rounded card over a dark scrim, centred. Touches outside the card do nothing. */
    private fun showCard(title: String, body: View, buttons: List<View>, scrollBody: Boolean = false) {
        hideOverlay()
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = roundedBox(palette.surface, dpF(24f))
            setPadding(dp(22), dp(18), dp(22), dp(18))
        }
        card.addView(TextView(this).apply {
            text = title
            setTextColor(palette.onSurface)
            textSize = 20f
            setTypeface(null, Typeface.BOLD)
        })
        val bodyView: View = if (scrollBody) {
            ScrollView(this).apply { addView(body) }
        } else {
            body
        }
        card.addView(bodyView, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            if (scrollBody) 0 else LinearLayout.LayoutParams.WRAP_CONTENT,
            if (scrollBody) 1f else 0f,
        ).apply { topMargin = dp(12) })
        card.addView(LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.END
            buttons.forEachIndexed { i, b ->
                addView(b, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                    if (i > 0) leftMargin = dp(10)
                })
            }
        }, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(16)
        })

        val scrim = FrameLayout(this).apply {
            setBackgroundColor(palette.scrim)
            isClickable = true
        }
        val maxWidth = minOf(dp(480), resources.displayMetrics.widthPixels - dp(48))
        val maxHeight = resources.displayMetrics.heightPixels - dp(40)
        scrim.addView(card, FrameLayout.LayoutParams(maxWidth, if (scrollBody) maxHeight else FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER))
        root.addView(scrim, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
        overlay = scrim
    }

    private fun hideOverlay() {
        overlay?.let { root.removeView(it) }
        overlay = null
    }

    private enum class Kind { FILLED, TONAL, DANGER }

    /** Rounded button with ripple. FILLED uses primary, TONAL primary container, DANGER the error colour. */
    private fun pill(label: String, kind: Kind, onClick: () -> Unit): TextView {
        val (bg, fg) = when (kind) {
            Kind.FILLED -> palette.primary to palette.onPrimary
            Kind.TONAL -> palette.primaryContainer to palette.onPrimaryContainer
            Kind.DANGER -> palette.error to Color.WHITE
        }
        return TextView(this).apply {
            text = label
            setTextColor(fg)
            textSize = 16f
            setTypeface(null, Typeface.BOLD)
            gravity = Gravity.CENTER
            setPadding(dp(18), dp(10), dp(18), dp(10))
            background = rippled(roundedBox(bg, dpF(24f)), Color.argb(60, 255, 255, 255))
            isClickable = true
            isFocusable = true
            setOnClickListener { onClick() }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
                leftMargin = dp(8)
            }
        }
    }

    companion object {
        private const val PREPARING_TEXT = "מכין את המשחק...\nPreparing..."
    }
}
