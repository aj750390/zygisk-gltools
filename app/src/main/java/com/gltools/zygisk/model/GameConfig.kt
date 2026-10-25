package com.gltools.zygisk.model

import java.io.Serializable

data class DevicePreset(
    val vendor: String,
    val renderer: String,
    val version: String,
    val model: String,
    val manufacturer: String,
    val brand: String,
    val device: String,
    val board: String,
    val hardware: String
) : Serializable

data class GameConfig(
    val packageName: String,
    val appName: String,
    var enabled: Boolean = false,
    var presetName: String = "Adreno 750 (Galaxy S24 Ultra)",
    var vendor: String = "Qualcomm",
    var renderer: String = "Adreno (TM) 750",
    var version: String = "OpenGL ES 3.2 V@0615.0 (GLTools)",
    var model: String = "SM-S928B",
    var manufacturer: String = "samsung",
    var brand: String = "samsung",
    var device: String = "e3q",
    var board: String = "pineapple",
    var hardware: String = "qcom",
    var downscaleFactor: Float = 1.0f,
    var fpsLimit: Int = 0,
    var optimizeTextures: Boolean = false,
    var forceFp16Shaders: Boolean = false,
    var shaderPreset: String = "None (Default Game Shaders)",
    var customGlsl: String = ""
) : Serializable {
    companion object {
        val GL_VERSION_PRESETS = listOf(
            "OpenGL ES 3.2 V@0615.0 (GLTools)",
            "OpenGL ES 3.2 V@125.0 (Mali Immortalis)",
            "OpenGL ES 3.1 V@0502.0 (GLTools)",
            "OpenGL ES 3.0 V@0415.0 (GLTools)",
            "OpenGL ES 2.0 (Legacy Mode)"
        )

        val SHADER_PRESETS = listOf(
            "None (Default Game Shaders)",
            "Vibrant HDR (Saturation & Contrast)",
            "Golden Hour (Warm Sun & Bloom)",
            "ACES Filmic HDR Tonemap",
            "Cyberpunk Neon (Cool Contrast)",
            "Cel-Shading (Toon Posterize)",
            "Night Vision (Tactical Green)",
            "Custom GLSL Expression"
        )

        val PRESETS = mapOf(
            "Adreno 750 (Galaxy S24 Ultra)" to DevicePreset(
                vendor = "Qualcomm",
                renderer = "Adreno (TM) 750",
                version = "OpenGL ES 3.2 V@0615.0 (GLTools)",
                model = "SM-S928B",
                manufacturer = "samsung",
                brand = "samsung",
                device = "e3q",
                board = "pineapple",
                hardware = "qcom"
            ),
            "Adreno 740 (ROG Phone 7)" to DevicePreset(
                vendor = "Qualcomm",
                renderer = "Adreno (TM) 740",
                version = "OpenGL ES 3.2 V@0602.0 (GLTools)",
                model = "ASUS_AI2205",
                manufacturer = "asus",
                brand = "asus",
                device = "ASUS_AI2205",
                board = "kalama",
                hardware = "qcom"
            ),
            "Mali-G720 (Vivo X100 Pro)" to DevicePreset(
                vendor = "ARM",
                renderer = "Mali-G720 Immortalis MC12",
                version = "OpenGL ES 3.2 v1.r46p0 (GLTools)",
                model = "V2309A",
                manufacturer = "vivo",
                brand = "vivo",
                device = "V2309A",
                board = "k6989v1_64",
                hardware = "mt6989"
            ),
            "Apple A17 Pro (iPhone 15 Pro)" to DevicePreset(
                vendor = "Apple",
                renderer = "Apple A17 GPU",
                version = "OpenGL ES 3.0 Metal-310.1 (GLTools)",
                model = "iPhone16,1",
                manufacturer = "Apple",
                brand = "Apple",
                device = "D83AP",
                board = "t8130",
                hardware = "t8130"
            ),
            "NVIDIA Tegra X1 (Shield TV)" to DevicePreset(
                vendor = "NVIDIA Corporation",
                renderer = "NVIDIA Tegra",
                version = "OpenGL ES 3.2 NVIDIA 418.00 (GLTools)",
                model = "SHIELD Android TV",
                manufacturer = "NVIDIA",
                brand = "NVIDIA",
                device = "foster",
                board = "foster",
                hardware = "tegra"
            )
        )
    }
}
