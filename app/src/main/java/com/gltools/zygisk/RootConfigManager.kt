package com.gltools.zygisk

import android.util.Base64
import java.io.File

object RootConfigManager {
    private const val CONFIG_DIR = "/data/adb/gltools/packages"
    private val configCache = mutableMapOf<String, GameConfig>()

    fun refreshAllConfigs() {
        configCache.clear()
        try {
            val process = Runtime.getRuntime().exec(
                arrayOf("su", "-c", "for f in $CONFIG_DIR/*.conf; do [ -f \"\$f\" ] && echo \"===FILE:\$f===\" && cat \"\$f\"; done")
            )
            val output = process.inputStream.bufferedReader().readText()
            process.waitFor()

            var currentPkg = ""
            val sb = StringBuilder()

            fun flushCurrent() {
                if (currentPkg.isNotEmpty()) {
                    configCache[currentPkg] = parseConfigText(currentPkg, sb.toString())
                }
            }

            for (line in output.lines()) {
                if (line.startsWith("===FILE:") && line.endsWith("===")) {
                    flushCurrent()
                    sb.clear()
                    val path = line.removePrefix("===FILE:").removeSuffix("===")
                    currentPkg = File(path).name.removeSuffix(".conf")
                } else {
                    sb.append(line).append("\n")
                }
            }
            flushCurrent()
        } catch (_: Exception) {}
    }

    fun getCachedConfig(packageName: String): GameConfig {
        return configCache[packageName] ?: GameConfig(packageName = packageName)
    }

    fun loadConfig(packageName: String): GameConfig {
        val targetPath = "$CONFIG_DIR/$packageName.conf"
        return try {
            val directFile = File(targetPath)
            val text = if (directFile.exists() && directFile.canRead()) {
                directFile.readText()
            } else {
                val proc = Runtime.getRuntime().exec(arrayOf("su", "-c", "cat $targetPath 2>/dev/null"))
                val out = proc.inputStream.bufferedReader().readText()
                proc.waitFor()
                out
            }
            if (text.isNotBlank()) {
                val cfg = parseConfigText(packageName, text)
                configCache[packageName] = cfg
                cfg
            } else {
                configCache[packageName] ?: GameConfig(packageName = packageName)
            }
        } catch (e: Exception) {
            configCache[packageName] ?: GameConfig(packageName = packageName)
        }
    }

    private fun parseConfigText(packageName: String, text: String): GameConfig {
        val config = GameConfig(packageName = packageName)
        for (line in text.lines()) {
            val parts = line.split("=", limit = 2)
            if (parts.size == 2) {
                val key = parts[0].trim()
                val value = parts[1].trim()
                when (key) {
                    "enabled" -> config.enabled = (value == "true" || value == "1")
                    "targetGlMajor" -> config.targetGlMajor = value.toIntOrNull() ?: 0
                    "targetGlMinor" -> config.targetGlMinor = value.toIntOrNull() ?: 0
                    "renderer" -> config.renderer = value
                    "vendor" -> config.vendor = value
                    "version" -> config.version = value
                    "extensions" -> config.extensions = value
                    "resolutionScale" -> config.resolutionScale = value.toFloatOrNull() ?: 1.0f
                    "force16BitTextures" -> config.force16BitTextures = (value == "true" || value == "1")
                    "forceFp16Shaders" -> config.forceFp16Shaders = (value == "true" || value == "1")
                    "shaderPreset" -> config.shaderPreset = value
                    "customGlsl" -> config.customGlsl = value
                    "frameGenMode" -> config.frameGenMode = value
                    "fakeCpuEnabled" -> config.fakeCpuEnabled = (value == "true" || value == "1")
                    "fakeCpuPreset" -> config.fakeCpuPreset = value
                    "fakeCpuName" -> config.fakeCpuName = value
                    "fakeCpuHardware" -> config.fakeCpuHardware = value
                    "fakeCpuPlatform" -> config.fakeCpuPlatform = value
                    "fakeCpuCores" -> config.fakeCpuCores = value.toIntOrNull() ?: 8
                }
            }
        }
        return config
    }

    fun saveConfig(config: GameConfig): Boolean {
        val sb = StringBuilder()
        sb.append("enabled=${config.enabled}\n")
        sb.append("packageName=${config.packageName}\n")
        sb.append("targetGlMajor=${config.targetGlMajor}\n")
        sb.append("targetGlMinor=${config.targetGlMinor}\n")
        sb.append("renderer=${config.renderer}\n")
        sb.append("vendor=${config.vendor}\n")
        sb.append("version=${config.version}\n")
        sb.append("extensions=${config.extensions}\n")
        sb.append("resolutionScale=${config.resolutionScale}\n")
        sb.append("force16BitTextures=${config.force16BitTextures}\n")
        sb.append("forceFp16Shaders=${config.forceFp16Shaders}\n")
        sb.append("shaderPreset=${config.shaderPreset}\n")
        sb.append("customGlsl=${config.customGlsl}\n")
        sb.append("frameGenMode=${config.frameGenMode}\n")
        sb.append("fakeCpuEnabled=${config.fakeCpuEnabled}\n")
        sb.append("fakeCpuPreset=${config.fakeCpuPreset}\n")
        sb.append("fakeCpuName=${config.fakeCpuName}\n")
        sb.append("fakeCpuHardware=${config.fakeCpuHardware}\n")
        sb.append("fakeCpuPlatform=${config.fakeCpuPlatform}\n")
        sb.append("fakeCpuCores=${config.fakeCpuCores}\n")

        val content = sb.toString()
        val targetPath = "$CONFIG_DIR/${config.packageName}.conf"
        val b64 = Base64.encodeToString(content.toByteArray(Charsets.UTF_8), Base64.NO_WRAP)

        // Method 1: Direct su -c with Base64 decode (immune to heredoc indentation & special chars)
        try {
            val cmd = "mkdir -p $CONFIG_DIR && chmod 755 /data/adb/gltools $CONFIG_DIR && echo '$b64' | base64 -d > $targetPath && chmod 644 $targetPath"
            val proc = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
            if (proc.waitFor() == 0) {
                configCache[config.packageName] = config
                return true
            }
        } catch (_: Exception) {}

        // Method 2: Interactive su shell piping (fallback)
        return try {
            val process = Runtime.getRuntime().exec("su")
            val os = process.outputStream
            val script = "mkdir -p $CONFIG_DIR\n" +
                "chmod 755 /data/adb/gltools $CONFIG_DIR\n" +
                "echo '$b64' | base64 -d > $targetPath\n" +
                "chmod 644 $targetPath\n" +
                "exit\n"
            os.write(script.toByteArray(Charsets.UTF_8))
            os.flush()
            os.close()
            val ok = process.waitFor() == 0
            if (ok) {
                configCache[config.packageName] = config
            }
            ok
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }
}
