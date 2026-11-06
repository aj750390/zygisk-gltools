package com.gltools.zygisk

data class GameConfig(
    var enabled: Boolean = false,
    var packageName: String = "",
    var targetGlMajor: Int = 0,
    var targetGlMinor: Int = 0,
    var renderer: String = "",
    var vendor: String = "",
    var version: String = "",
    var extensions: String = "",
    var resolutionScale: Float = 1.0f,
    var force16BitTextures: Boolean = false,
    var forceFp16Shaders: Boolean = false,
    var shaderPreset: String = "None",
    var customGlsl: String = "",
    var frameGenMode: String = "Off",

    // Fake CPU & Cores
    var fakeCpuEnabled: Boolean = false,
    var fakeCpuPreset: String = "None",
    var fakeCpuName: String = "",
    var fakeCpuHardware: String = "",
    var fakeCpuPlatform: String = "",
    var fakeCpuCores: Int = 8
)
