package com.gltools.zygisk.model

import java.io.Serializable

data class GameConfig(
    val packageName: String,
    val appName: String,
    var enabled: Boolean = true,
    var presetName: String = "Adreno 750 (Snapdragon 8 Gen 3)",
    var vendor: String = "Qualcomm",
    var renderer: String = "Adreno (TM) 750",
    var version: String = "OpenGL ES 3.2 V@0615.0",
    var downscaleFactor: Float = 1.0f,
    var fpsLimit: Int = 0,
    var optimizeTextures: Boolean = false
) : Serializable {
    companion object {
        val PRESETS = mapOf(
            "Adreno 750 (Snapdragon 8 Gen 3)" to Pair("Qualcomm", "Adreno (TM) 750"),
            "Adreno 740 (Snapdragon 8 Gen 2)" to Pair("Qualcomm", "Adreno (TM) 740"),
            "Mali-G720 (Dimensity 9300)" to Pair("ARM", "Mali-G720 Immortalis MC12"),
            "Apple A17 Pro GPU" to Pair("Apple", "Apple A17 GPU"),
            "NVIDIA Tegra X1" to Pair("NVIDIA Corporation", "NVIDIA Tegra")
        )
    }
}
