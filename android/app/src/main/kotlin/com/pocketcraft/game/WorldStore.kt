package com.pocketcraft.game

import android.content.Context
import java.io.File
import kotlin.random.Random

/**
 * Worlds live in `filesDir/worlds/<name>/` (the engine appends the same path in Game::onCreate, so
 * the folder name is the world name passed to nativeOnCreate). Last-played time, the seed a world was
 * created with and the last world played are kept in SharedPreferences `pocketcraft_home`.
 *
 * Every filesystem or preferences call is safe to repeat; failures are ignored so the home screen
 * keeps working.
 */
class WorldStore(context: Context) {
    data class WorldInfo(val name: String, val lastPlayed: Long)

    private val appContext = context.applicationContext
    private val worldsDir = File(appContext.filesDir, WORLDS_DIR)
    private val prefs = appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /** Every world folder, most recently played first. */
    fun list(): List<WorldInfo> {
        val dirs = worldsDir.listFiles()?.filter { it.isDirectory } ?: return emptyList()
        return dirs.map { dir ->
            WorldInfo(dir.name, prefs.getLong(KEY_PLAYED + dir.name, dir.lastModified().takeIf { it > 0 } ?: 0L))
        }.sortedByDescending { it.lastPlayed }
    }

    fun exists(name: String): Boolean = File(worldsDir, name).isDirectory

    /** The last world played, if it still exists. */
    fun lastWorld(): String? = prefs.getString(KEY_LAST_WORLD, null)?.takeIf { exists(it) }

    fun seedOf(name: String): Long = prefs.getLong(KEY_SEED + name, 0L)

    /** Returns an error message for an unusable name, or null when the name is fine. */
    fun checkName(raw: String): String? {
        val name = raw.trim()
        return when {
            name.isEmpty() -> "Enter a name / נא להזין שם"
            name.length > MAX_NAME -> "Name is too long / השם ארוך מדי"
            name == "." || name == ".." -> "Invalid name / שם לא חוקי"
            name.any { it in FORBIDDEN || it.isISOControl() } -> "Invalid characters / תווים לא חוקיים"
            exists(name) -> "A world with this name exists / כבר יש עולם בשם הזה"
            else -> null
        }
    }

    /** Creates the folder and remembers the seed. Name must have passed [checkName]. */
    fun create(rawName: String, seed: Long): String {
        val name = rawName.trim()
        runCatching { File(worldsDir, name).mkdirs() }
        prefs.edit().putLong(KEY_SEED + name, seed).apply()
        return name
    }

    /** Deletes the world folder and its preferences. */
    fun delete(name: String) {
        runCatching { File(worldsDir, name).deleteRecursively() }
        val edit = prefs.edit().remove(KEY_PLAYED + name).remove(KEY_SEED + name)
        if (prefs.getString(KEY_LAST_WORLD, null) == name) edit.remove(KEY_LAST_WORLD)
        edit.apply()
    }

    fun markPlayed(name: String) {
        prefs.edit()
            .putLong(KEY_PLAYED + name, System.currentTimeMillis())
            .putString(KEY_LAST_WORLD, name)
            .apply()
    }

    companion object {
        private const val WORLDS_DIR = "worlds"
        private const val PREFS_NAME = "pocketcraft_home"
        private const val KEY_LAST_WORLD = "last_world"
        private const val KEY_PLAYED = "played_"
        private const val KEY_SEED = "seed_"
        private const val MAX_NAME = 32
        private const val FORBIDDEN = "/\\:*?\"<>|"

        /** A random 32-bit seed (the engine stores seeds as uint32). */
        fun randomSeed(): Long = Random.nextInt().toLong() and 0xFFFFFFFFL
    }
}
