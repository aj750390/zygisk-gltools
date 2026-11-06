package com.gltools.zygisk

import java.io.File

object RootConfigManager {
    private const val CONFIG_DIR = "/data/adb/gltools/packages"

    fun loadConfig(packageName: String): GameConfig {
        val config = GameConfig(packageName = packageName)
        try {
            val file = File("$CONFIG_DIR/$packageName.conf")
            if (file.exists()) {
                file.readLines().forEach { line ->
                    val parts = line.split("=", limit = 2)
                    if (parts.size == 2) {
                        val key = parts[0].trim()
                        val value = parts[1].trim()
                        when (key) {
                            "enabled" -> config.enabled = value.toBoolean()
                            "targetGlMajor" -> config.targetGlMajor = value.toIntOrNull() ?: 0
                            "targetGlMinor" -> config.targetGlMinor = value.toIntOrNull() ?: 0
                            "renderer" -> config.renderer = value
                            "vendor" -> config.vendor = value
                            "version" -> config.version = value
                            "extensions" -> config.extensions = value
                            "resolutionScale" -> config.resolutionScale = value.toFloatOrNull() ?: 1.0f
                            "force16BitTextures" -> config.force16BitTextures = value.toBoolean()
                            "forceFp16Shaders" -> config.forceFp16Shaders = value.toBoolean()
                            "shaderPreset" -> config.shaderPreset = value
                            "customGlsl" -> config.customGlsl = value
                            "frameGenMode" -> config.frameGenMode = value
                            "fakeCpuEnabled" -> config.fakeCpuEnabled = value.toBoolean()
                            "fakeCpuPreset" -> config.fakeCpuPreset = value
                            "fakeCpuName" -> config.fakeCpuName = value
                            "fakeCpuHardware" -> config.fakeCpuHardware = value
                            "fakeCpuPlatform" -> config.fakeCpuPlatform = value
                            "fakeCpuCores" -> config.fakeCpuCores = value.toIntOrNull() ?: 8
                        }
                    }
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
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
        val tempPath = "/data/local/tmp/${config.packageName}.conf"
        val targetPath = "$CONFIG_DIR/${config.packageName}.conf"

        return try {
            val process = Runtime.getRuntime().exec("su")
            val os = process.outputStream
            val script = """
                mkdir -p $CONFIG_DIR
                cat << EOF > $targetPath
                $content
                EOF
                chmod 644 $targetPath
            """.trimIndent()
            os.write(script.toByteArray())
            os.flush()
            os.close()
            process.waitFor() == 0
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }
}
