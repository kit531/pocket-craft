package com.pocketcraft.game

import android.content.Context
import android.util.Log
import java.io.File
import java.nio.file.Files

/**
 * One-time cleanup of the old Java launcher's private data. Pocket Craft has the same applicationId
 * (com.movtery.zalithlauncher) and installs as an update, so Android keeps the old app's files, which
 * can be gigabytes. The owner agreed to delete everything the old app stored.
 *
 * Runs once: when [MARKER_NAME] exists in filesDir nothing happens. The marker is written after the
 * pass finishes, even if some items could not be deleted (those are logged), so the game's own
 * worlds are never deleted by a later start.
 *
 * Every filesystem call is wrapped so a failure never crashes the app. Symlinks are never followed.
 */
object LegacyDataCleaner {
    private const val TAG = "PocketCraftCleanup"
    private const val MARKER_NAME = ".pocketcraft_initialized"

    /** SharedPreferences file that Pocket Craft itself uses (GameSettings). Kept. */
    private const val KEEP_PREFS = "pocketcraft_settings.xml"

    private val lock = Any()

    /** True when the cleanup already ran. Cheap, safe on the UI thread. */
    fun isDone(context: Context): Boolean = markerFile(context).exists()

    /**
     * Runs the cleanup if it has not run yet, then writes the marker. Blocking: call it from a
     * background thread. Concurrent callers (for example a second Activity instance) wait for the
     * first one and then return, because the marker exists by then.
     */
    fun cleanIfNeeded(context: Context) {
        synchronized(lock) {
            try {
                if (isDone(context)) return
                val freed = cleanAll(context)
                Log.i(TAG, "legacy cleanup freed $freed bytes (${freed / (1024L * 1024L)} MiB)")
                writeMarker(context)
            } catch (e: Exception) {
                Log.e(TAG, "legacy cleanup failed", e)
            }
        }
    }

    private fun markerFile(context: Context): File = File(context.filesDir, MARKER_NAME)

    private fun writeMarker(context: Context) {
        try {
            markerFile(context).createNewFile()
        } catch (e: Exception) {
            Log.e(TAG, "could not write cleanup marker", e)
        }
    }

    private fun cleanAll(context: Context): Long {
        var freed = 0L
        freed += deleteChildren(context.filesDir, "filesDir", keep = MARKER_NAME)
        freed += deleteChildren(context.cacheDir, "cacheDir")
        freed += deleteChildren(context.codeCacheDir, "codeCacheDir")
        freed += deleteChildren(context.noBackupFilesDir, "noBackupFilesDir")
        freed += deleteChildren(safeExternalFilesDir(context), "externalFilesDir")
        freed += deleteChildren(safeExternalCacheDir(context), "externalCacheDir")
        freed += deleteChildren(File(context.applicationInfo.dataDir, "shared_prefs"), "shared_prefs", keep = KEEP_PREFS)
        freed += deleteDatabases(context)
        return freed
    }

    private fun safeExternalFilesDir(context: Context): File? = try {
        context.getExternalFilesDir(null)
    } catch (e: Exception) {
        Log.w(TAG, "external files dir unavailable", e)
        null
    }

    private fun safeExternalCacheDir(context: Context): File? = try {
        context.externalCacheDir
    } catch (e: Exception) {
        Log.w(TAG, "external cache dir unavailable", e)
        null
    }

    /** Deletes every child of [dir] except the name [keep]. Returns the bytes removed. */
    private fun deleteChildren(dir: File?, label: String, keep: String? = null): Long {
        if (dir == null) return 0L
        return try {
            var freed = 0L
            val children = dir.listFiles() ?: return 0L
            for (child in children) {
                if (child.name == keep) continue
                freed += deleteTree(child)
            }
            freed
        } catch (e: Exception) {
            Log.w(TAG, "cleanup of $label failed", e)
            0L
        }
    }

    /**
     * Deletes [file] recursively without following symlinks (a link is removed, its target is not).
     * Returns the total size of the regular files that were removed.
     */
    private fun deleteTree(file: File): Long {
        var freed = 0L
        try {
            val link = isSymlink(file)
            if (!link && file.isDirectory) {
                val children = file.listFiles()
                if (children != null) {
                    for (child in children) freed += deleteTree(child)
                }
            } else if (!link && file.isFile) {
                val length = file.length()
                if (file.delete()) freed += length else Log.w(TAG, "could not delete ${file.path}")
                return freed
            }
            // Directory (now empty) or symlink.
            if (!file.delete() && file.exists()) Log.w(TAG, "could not delete ${file.path}")
        } catch (e: Exception) {
            Log.w(TAG, "could not delete ${file.path}", e)
        }
        return freed
    }

    /** Conservative: if the check itself fails, the entry is treated as a link and never followed. */
    private fun isSymlink(file: File): Boolean = try {
        Files.isSymbolicLink(file.toPath())
    } catch (e: Exception) {
        true
    }

    /** Deletes the old launcher's SQLite databases (and their journal/WAL/SHM files). */
    private fun deleteDatabases(context: Context): Long {
        return try {
            var freed = 0L
            for (name in context.databaseList()) {
                val db = context.getDatabasePath(name)
                val parts = listOf(
                    db,
                    File(db.path + "-journal"),
                    File(db.path + "-wal"),
                    File(db.path + "-shm"),
                ).filter { it.isFile }
                var size = 0L
                for (part in parts) size += part.length()
                if (context.deleteDatabase(name)) {
                    freed += size
                } else {
                    Log.w(TAG, "could not delete database $name")
                }
            }
            freed
        } catch (e: Exception) {
            Log.w(TAG, "database cleanup failed", e)
            0L
        }
    }
}
