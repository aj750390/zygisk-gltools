package com.gltools.zygisk.util

import com.gltools.zygisk.model.GameConfig
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStreamWriter

object RootConfigManager {
    private const val MODULE_CONFIG_DIR = "/data/adb/modules/zygisk_gltools/config"
    private const val FALLBACK_CONFIG_DIR = "/data/local/tmp/gltools"

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
        val cleanCustomGlsl = config.customGlsl.replace("\n", " ").replace("\r", " ").trim()
        val confContent = buildString {
            appendLine("packageName=${config.packageName}")
            appendLine("enabled=${config.enabled}")
            appendLine("presetName=${config.presetName}")
            appendLine("vendor=${config.vendor}")
            appendLine("renderer=${config.renderer}")
            appendLine("version=${config.version}")
            appendLine("model=${config.model}")
            appendLine("manufacturer=${config.manufacturer}")
            appendLine("brand=${config.brand}")
            appendLine("device=${config.device}")
            appendLine("board=${config.board}")
            appendLine("hardware=${config.hardware}")
            appendLine("downscaleFactor=${config.downscaleFactor}")
            appendLine("fpsLimit=${config.fpsLimit}")
            appendLine("optimizeTextures=${config.optimizeTextures}")
            appendLine("forceFp16Shaders=${config.forceFp16Shaders}")
            appendLine("shaderPreset=${config.shaderPreset}")
            appendLine("customGlsl=$cleanCustomGlsl")
            appendLine("frameGenMode=${config.frameGenMode}")
        }

        val jsonContent = JSONObject().apply {
            put("packageName", config.packageName)
            put("enabled", config.enabled)
            put("presetName", config.presetName)
            put("vendor", config.vendor)
            put("renderer", config.renderer)
            put("version", config.version)
            put("model", config.model)
            put("manufacturer", config.manufacturer)
            put("brand", config.brand)
            put("device", config.device)
            put("board", config.board)
            put("hardware", config.hardware)
            put("downscaleFactor", config.downscaleFactor.toDouble())
            put("fpsLimit", config.fpsLimit)
            put("optimizeTextures", config.optimizeTextures)
            put("forceFp16Shaders", config.forceFp16Shaders)
            put("shaderPreset", config.shaderPreset)
            put("customGlsl", cleanCustomGlsl)
            put("frameGenMode", config.frameGenMode)
        }.toString(2)

        val pkg = config.packageName
        val script = """
            mkdir -p $MODULE_CONFIG_DIR $FALLBACK_CONFIG_DIR
            cat << 'EOF_CONF' > $MODULE_CONFIG_DIR/$pkg.conf
$confContent
EOF_CONF
            cat << 'EOF_JSON' > $MODULE_CONFIG_DIR/$pkg.json
$jsonContent
EOF_JSON
            cp $MODULE_CONFIG_DIR/$pkg.conf $FALLBACK_CONFIG_DIR/$pkg.conf
            cp $MODULE_CONFIG_DIR/$pkg.json $FALLBACK_CONFIG_DIR/$pkg.json
            chmod -R 777 $MODULE_CONFIG_DIR $FALLBACK_CONFIG_DIR
            chcon -R u:object_r:system_file:s0 $MODULE_CONFIG_DIR 2>/dev/null || true
        """.trimIndent()

        return executeSuScript(script)
    }

    fun loadConfig(packageName: String, appName: String): GameConfig {
        val output = executeSuCommandWithOutput(
            "cat $MODULE_CONFIG_DIR/$packageName.conf 2>/dev/null || cat $FALLBACK_CONFIG_DIR/$packageName.conf 2>/dev/null || cat $MODULE_CONFIG_DIR/$packageName.json 2>/dev/null"
        )
        return parseConfigOutput(packageName, appName, output)
    }

    fun loadAllSavedConfigs(): Map<String, GameConfig> {
        val result = mutableMapOf<String, GameConfig>()
        val script = """
            for f in $MODULE_CONFIG_DIR/*.conf $FALLBACK_CONFIG_DIR/*.conf; do
                if [ -f "${'$'}f" ]; then
                    echo "===FILE_START==="
                    cat "${'$'}f"
                    echo "===FILE_END==="
                fi
            done
        """.trimIndent()

        val raw = executeSuCommandWithOutput(script)
        if (raw.isBlank()) return result

        val blocks = raw.split("===FILE_START===")
        for (block in blocks) {
            val endIdx = block.indexOf("===FILE_END===")
            if (endIdx <= 0) continue
            val content = block.substring(0, endIdx).trim()
            val cfg = parseConfigOutput("", "", content)
            if (cfg.packageName.isNotEmpty()) {
                result[cfg.packageName] = cfg
            }
        }
        return result
    }

    private fun parseConfigOutput(defaultPkg: String, defaultAppName: String, output: String): GameConfig {
        val config = GameConfig(packageName = defaultPkg, appName = defaultAppName, enabled = false)
        if (output.isBlank()) return config

        if (output.trimStart().startsWith("{")) {
            try {
                val json = JSONObject(output)
                return GameConfig(
                    packageName = json.optString("packageName", defaultPkg),
                    appName = defaultAppName,
                    enabled = json.optBoolean("enabled", false),
                    presetName = json.optString("presetName", config.presetName),
                    vendor = json.optString("vendor", config.vendor),
                    renderer = json.optString("renderer", config.renderer),
                    version = json.optString("version", config.version),
                    model = json.optString("model", config.model),
                    manufacturer = json.optString("manufacturer", config.manufacturer),
                    brand = json.optString("brand", config.brand),
                    device = json.optString("device", config.device),
                    board = json.optString("board", config.board),
                    hardware = json.optString("hardware", config.hardware),
                    downscaleFactor = json.optDouble("downscaleFactor", 1.0).toFloat(),
                    fpsLimit = json.optInt("fpsLimit", 0),
                    optimizeTextures = json.optBoolean("optimizeTextures", false),
                    forceFp16Shaders = json.optBoolean("forceFp16Shaders", false),
                    shaderPreset = json.optString("shaderPreset", config.shaderPreset),
                    customGlsl = json.optString("customGlsl", ""),
                    frameGenMode = json.optString("frameGenMode", config.frameGenMode)
                )
            } catch (_: Exception) {
                return config
            }
        }

        var parsedPkg = defaultPkg
        output.lineSequence().forEach { rawLine ->
            val line = rawLine.trim()
            val eq = line.indexOf('=')
            if (eq > 0) {
                val key = line.substring(0, eq).trim()
                val value = line.substring(eq + 1).trim()
                when (key) {
                    "packageName" -> if (value.isNotEmpty()) parsedPkg = value
                    "enabled" -> config.enabled = (value == "true" || value == "1")
                    "presetName" -> if (value.isNotEmpty()) config.presetName = value
                    "vendor" -> if (value.isNotEmpty()) config.vendor = value
                    "renderer" -> if (value.isNotEmpty()) config.renderer = value
                    "version" -> if (value.isNotEmpty()) config.version = value
                    "model" -> if (value.isNotEmpty()) config.model = value
                    "manufacturer" -> if (value.isNotEmpty()) config.manufacturer = value
                    "brand" -> if (value.isNotEmpty()) config.brand = value
                    "device" -> if (value.isNotEmpty()) config.device = value
                    "board" -> if (value.isNotEmpty()) config.board = value
                    "hardware" -> if (value.isNotEmpty()) config.hardware = value
                    "downscaleFactor" -> config.downscaleFactor = value.toFloatOrNull() ?: 1.0f
                    "fpsLimit" -> config.fpsLimit = value.toIntOrNull() ?: 0
                    "optimizeTextures" -> config.optimizeTextures = (value == "true" || value == "1")
                    "forceFp16Shaders" -> config.forceFp16Shaders = (value == "true" || value == "1")
                    "shaderPreset" -> if (value.isNotEmpty()) config.shaderPreset = value
                    "customGlsl" -> config.customGlsl = value
                    "frameGenMode" -> if (value.isNotEmpty()) config.frameGenMode = value
                }
            }
        }
        return if (parsedPkg != defaultPkg) config.copy(packageName = parsedPkg) else config
    }

    private fun executeSuScript(script: String): Boolean {
        return try {
            val process = Runtime.getRuntime().exec("su")
            OutputStreamWriter(process.outputStream).use { writer ->
                writer.write(script)
                writer.write("\nexit\n")
                writer.flush()
            }
            process.waitFor() == 0
        } catch (e: Exception) {
            false
        }
    }

    private fun executeSuCommandWithOutput(script: String): String {
        return try {
            val process = Runtime.getRuntime().exec("su")
            OutputStreamWriter(process.outputStream).use { writer ->
                writer.write(script)
                writer.write("\nexit\n")
                writer.flush()
            }
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val sb = StringBuilder()
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                sb.appendLine(line)
            }
            process.waitFor()
            sb.toString()
        } catch (e: Exception) {
            ""
        }
    }
}
