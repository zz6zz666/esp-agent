package com.crushclaw

import android.app.Application
import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileOutputStream

class CrushClawApp : Application() {

    companion object {
        private const val TAG = "CrushClawApp"
        lateinit var instance: CrushClawApp
            private set
        private const val SEEDED_MARKER = "skills/cap_im_feishu/SKILL.md"
        private const val EMOTE_SEEDED_MARKER = "assets/284_240/.seeded"
    }

    val clawDataDir: File
        get() = filesDir.resolve(".crush-claw")

    val configFile: File
        get() = clawDataDir.resolve("config.json")

    val sessionDir: File
        get() = clawDataDir.resolve("sessions")

    val memoryDir: File
        get() = clawDataDir.resolve("memory")

    val skillsDir: File
        get() = clawDataDir.resolve("skills")

    val scriptsDir: File
        get() = clawDataDir.resolve("scripts")

    val routerRulesDir: File
        get() = clawDataDir.resolve("router_rules")

    val schedulerDir: File
        get() = clawDataDir.resolve("scheduler")

    val inboxDir: File
        get() = clawDataDir.resolve("inbox")

    val assetsDir: File
        get() = clawDataDir.resolve("assets")

    override fun onCreate() {
        super.onCreate()
        instance = this
        ensureDataDirs()
        seedDefaultsIfNeeded()
        seedEmoteAssetsIfNeeded()
        migrateFromExternalIfNeeded()
    }

    private fun ensureDataDirs() {
        val dirs = listOf(
            clawDataDir, sessionDir, memoryDir, skillsDir,
            scriptsDir, routerRulesDir, schedulerDir, inboxDir,
            assetsDir
        )
        dirs.forEach { it.mkdirs() }
    }

    /**
     * On first run, copy default skills/scripts/router_rules from APK assets
     * into the data directory. Uses a marker file to detect already-seeded state.
     */
    private fun seedDefaultsIfNeeded() {
        val marker = File(clawDataDir, SEEDED_MARKER)
        if (marker.exists()) {
            Log.i(TAG, "Defaults already seeded (marker found)")
            return
        }
        try {
            copyAssetsDir("defaults", clawDataDir)
            Log.i(TAG, "Defaults seeded from assets")
        } catch (e: Exception) {
            Log.w(TAG, "Failed to seed defaults: ${e.message}")
        }
    }

    private fun seedEmoteAssetsIfNeeded() {
        val marker = File(clawDataDir, EMOTE_SEEDED_MARKER)
        if (marker.exists()) return
        try {
            copyAssetsDir("284_240", assetsDir.resolve("284_240"))
            assetsDir.resolve("284_240").resolve(".seeded").createNewFile()
            Log.i(TAG, "Emote assets seeded from APK")
        } catch (e: Exception) {
            Log.w(TAG, "Failed to seed emote assets: ${e.message}")
        }
    }

    private fun copyAssetsDir(assetPath: String, destDir: File) {
        val children = assets.list(assetPath) ?: return
        destDir.mkdirs()
        for (child in children) {
            val childPath = "$assetPath/$child"
            val childDest = File(destDir, child)
            try {
                assets.open(childPath).use { input ->
                    childDest.parentFile?.mkdirs()
                    FileOutputStream(childDest).use { output ->
                        input.copyTo(output)
                    }
                }
            } catch (e: java.io.FileNotFoundException) {
                copyAssetsDir(childPath, childDest)
            }
        }
    }

    /**
     * If user previously ran the Windows version with data on external storage,
     * copy config.json to internal data dir on first run.
     */
    private fun migrateFromExternalIfNeeded() {
        if (configFile.exists()) return
        val externalBase = File("/sdcard/.crush-claw")
        val externalConfig = File(externalBase, "config.json")
        if (externalConfig.exists()) {
            try {
                externalConfig.copyTo(configFile, overwrite = false)
                Log.i(TAG, "Migrated config from external storage")
            } catch (e: Exception) {
                Log.w(TAG, "Failed to migrate config: ${e.message}")
            }
        }
    }

    /**
     * Path used by the C layer (mounted via JNI).
     * Returns the absolute path to the data directory.
     */
    fun clawDataDirPath(): String = clawDataDir.absolutePath
}
