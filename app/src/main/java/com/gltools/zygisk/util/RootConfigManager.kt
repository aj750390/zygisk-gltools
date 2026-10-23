package com.gltools.zygisk.util

import com.gltools.zygisk.model.GameConfig
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader

object RootConfigManager {
    private const val MODULE_CONFIG_DIR = "/data/adb/modules/zygisk_gltools/config"

    fun isRootAvailable(): Boolean {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "id"))
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val line = reader.readLine()
            process.waitFor()
            line != null && line.contains("uid=0")
        } catch (e: Exception) {
            false
        }
    }

    fun saveConfig(config: GameConfig): Boolean {
        val json = JSONObject().apply {
            put("packageName", config.packageName)
            put("enabled", config.enabled)
            put("vendor", config.vendor)
            put("renderer", config.renderer)
            put("version", config.version)
            put("downscaleFactor", config.downscaleFactor.toDouble())
            put("fpsLimit", config.fpsLimit)
            put("optimizeTextures", config.optimizeTextures)
        }.toString()

        val filePath = "$MODULE_CONFIG_DIR/${config.packageName}.json"
        val command = "mkdir -p $MODULE_CONFIG_DIR && echo '$json' > $filePath && chmod 666 $filePath"

        return executeSuCommand(command)
    }

    fun loadConfig(packageName: String, appName: String): GameConfig {
        val filePath = "$MODULE_CONFIG_DIR/$packageName.json"
        val output = executeSuCommandWithOutput("cat $filePath")

        if (output.isNotBlank()) {
            try {
                val json = JSONObject(output)
                return GameConfig(
                    packageName = packageName,
                    appName = appName,
                    enabled = json.optBoolean("enabled", true),
                    vendor = json.optString("vendor", "Qualcomm"),
                    renderer = json.optString("renderer", "Adreno (TM) 750"),
                    version = json.optString("version", "OpenGL ES 3.2 V@0615.0"),
                    downscaleFactor = json.optDouble("downscaleFactor", 1.0).toFloat(),
                    fpsLimit = json.optInt("fpsLimit", 0),
                    optimizeTextures = json.optBoolean("optimizeTextures", false)
                )
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
        return GameConfig(packageName = packageName, appName = appName)
    }

    private fun executeSuCommand(command: String): Boolean {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", command))
            process.waitFor() == 0
        } catch (e: Exception) {
            false
        }
    }

    private fun executeSuCommandWithOutput(command: String): String {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", command))
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val sb = StringBuilder()
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                sb.append(line)
            }
            process.waitFor()
            sb.toString()
        } catch (e: Exception) {
            ""
        }
    }
}
