package com.gltools.zygisk

import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import kotlin.math.roundToInt

class AppDetailActivity : AppCompatActivity() {

    private lateinit var config: GameConfig
    private var packageName: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        packageName = intent.getStringExtra("EXTRA_PACKAGE_NAME") ?: ""
        if (packageName.isEmpty()) {
            finish()
            return
        }

        config = RootConfigManager.loadConfig(packageName)
        setupViews()
    }

    private fun addSectionHeader(layout: LinearLayout, title: String) {
        val header = TextView(this).apply {
            text = title
            textSize = 17f
            setTypeface(null, Typeface.BOLD)
            setPadding(0, 28, 0, 8)
        }
        layout.addView(header)
    }

    private fun setupViews() {
        val rootLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 48)
        }

        val scrollView = ScrollView(this).apply {
            addView(rootLayout)
        }
        setContentView(scrollView)

        // 1. Title & Master Enable Switch
        val titleText = TextView(this).apply {
            text = "Target: $packageName"
            textSize = 19f
            setTypeface(null, Typeface.BOLD)
            setPadding(0, 0, 0, 16)
        }
        rootLayout.addView(titleText)

        val enableSwitch = Switch(this).apply {
            text = "Enable GLTools Hooks for this App"
            textSize = 16f
            isChecked = config.enabled
            setOnCheckedChangeListener { _, isChecked -> config.enabled = isChecked }
            setPadding(0, 0, 0, 16)
        }
        rootLayout.addView(enableSwitch)

        // -------------------------------------------------------------
        // 2. Fake CPU & Cores Emulation (GLTools Feature)
        // -------------------------------------------------------------
        addSectionHeader(rootLayout, "1. CPU & Core Spoofing")

        val fakeCpuSwitch = Switch(this).apply {
            text = "Emulate Fake CPU & Core Count"
            isChecked = config.fakeCpuEnabled
            setOnCheckedChangeListener { _, isChecked -> config.fakeCpuEnabled = isChecked }
        }
        rootLayout.addView(fakeCpuSwitch)

        val cpuPresetSpinner = Spinner(this)
        val cpuPresets = arrayOf(
            "Keep Current / Custom",
            "Snapdragon 8 Gen 3 (8 Cores)",
            "Snapdragon 8 Gen 2 (8 Cores)",
            "Dimensity 9300 (8 Cores)",
            "Apple A17 Pro (6 Cores)",
            "Google Tensor G3 (9 Cores)"
        )
        cpuPresetSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, cpuPresets)
        rootLayout.addView(cpuPresetSpinner)

        val cpuNameEdit = EditText(this).apply {
            hint = "CPU Model Name (e.g. Qualcomm Snapdragon 8 Gen 3)"
            setText(config.fakeCpuName)
        }
        rootLayout.addView(cpuNameEdit)

        val cpuHwEdit = EditText(this).apply {
            hint = "Hardware / SoC ID (e.g. Qualcomm Technologies, Inc SM8650)"
            setText(config.fakeCpuHardware)
        }
        rootLayout.addView(cpuHwEdit)

        val coresLabel = TextView(this).apply {
            text = "Spoofed CPU Cores: ${config.fakeCpuCores}"
            setPadding(0, 8, 0, 4)
        }
        rootLayout.addView(coresLabel)

        val coresSeekBar = SeekBar(this).apply {
            max = 16
            progress = config.fakeCpuCores.coerceIn(1, 16)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                    val count = p.coerceAtLeast(1)
                    coresLabel.text = "Spoofed CPU Cores: $count"
                    config.fakeCpuCores = count
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}
                override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }
        rootLayout.addView(coresSeekBar)

        cpuPresetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, pos: Int, id: Long) {
                when (pos) {
                    1 -> {
                        cpuNameEdit.setText("Qualcomm Snapdragon 8 Gen 3")
                        cpuHwEdit.setText("Qualcomm Technologies, Inc SM8650")
                        config.fakeCpuPlatform = "pineapple"
                        config.fakeCpuPreset = cpuPresets[pos]
                        coresSeekBar.progress = 8
                    }
                    2 -> {
                        cpuNameEdit.setText("Qualcomm Snapdragon 8 Gen 2")
                        cpuHwEdit.setText("Qualcomm Technologies, Inc SM8550")
                        config.fakeCpuPlatform = "kalama"
                        config.fakeCpuPreset = cpuPresets[pos]
                        coresSeekBar.progress = 8
                    }
                    3 -> {
                        cpuNameEdit.setText("MediaTek Dimensity 9300")
                        cpuHwEdit.setText("MT6989")
                        config.fakeCpuPlatform = "mt6989"
                        config.fakeCpuPreset = cpuPresets[pos]
                        coresSeekBar.progress = 8
                    }
                    4 -> {
                        cpuNameEdit.setText("Apple A17 Pro")
                        cpuHwEdit.setText("Apple A17 Pro")
                        config.fakeCpuPlatform = "t8130"
                        config.fakeCpuPreset = cpuPresets[pos]
                        coresSeekBar.progress = 6
                    }
                    5 -> {
                        cpuNameEdit.setText("Google Tensor G3")
                        cpuHwEdit.setText("Tensor G3")
                        config.fakeCpuPlatform = "zuma"
                        config.fakeCpuPreset = cpuPresets[pos]
                        coresSeekBar.progress = 9
                    }
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        // -------------------------------------------------------------
        // 3. GPU & OpenGL ES Version Spoofing
        // -------------------------------------------------------------
        addSectionHeader(rootLayout, "2. GPU & OpenGL ES Spoofing")

        val gpuPresetSpinner = Spinner(this)
        val gpuPresets = arrayOf(
            "Keep Current / Custom",
            "Adreno 750 (Snapdragon 8 Gen 3)",
            "Adreno 740 (Snapdragon 8 Gen 2)",
            "Mali-G720 Immortalis MC12",
            "Apple A17 Pro GPU",
            "NVIDIA Tegra X1"
        )
        gpuPresetSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, gpuPresets)
        rootLayout.addView(gpuPresetSpinner)

        val rendererEdit = EditText(this).apply {
            hint = "GL_RENDERER (e.g. Adreno (TM) 750)"
            setText(config.renderer)
        }
        rootLayout.addView(rendererEdit)

        val vendorEdit = EditText(this).apply {
            hint = "GL_VENDOR (e.g. Qualcomm)"
            setText(config.vendor)
        }
        rootLayout.addView(vendorEdit)

        val versionEdit = EditText(this).apply {
            hint = "GL_VERSION (e.g. OpenGL ES 3.2 V@0762.0)"
            setText(config.version)
        }
        rootLayout.addView(versionEdit)

        val glesLabel = TextView(this).apply {
            text = "Target OpenGL ES Version:"
            setPadding(0, 8, 0, 4)
        }
        rootLayout.addView(glesLabel)

        val glesSpinner = Spinner(this)
        val glesOptions = arrayOf(
            "Default (No Override)",
            "OpenGL ES 3.2",
            "OpenGL ES 3.1",
            "OpenGL ES 3.0",
            "OpenGL ES 2.0"
        )
        glesSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, glesOptions)
        val initialGlesPos = when ("${config.targetGlMajor}.${config.targetGlMinor}") {
            "3.2" -> 1
            "3.1" -> 2
            "3.0" -> 3
            "2.0" -> 4
            else -> 0
        }
        glesSpinner.setSelection(initialGlesPos)
        rootLayout.addView(glesSpinner)

        // -------------------------------------------------------------
        // 4. GL_EXTENSIONS Spoofing
        // -------------------------------------------------------------
        addSectionHeader(rootLayout, "3. GL_EXTENSIONS Spoofing")

        val extPresetSpinner = Spinner(this)
        val extPresets = arrayOf(
            "Keep Current / Custom",
            "Adreno 750 Full Extensions",
            "Mali Immortalis Extensions",
            "NVIDIA Tegra Extensions"
        )
        extPresetSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, extPresets)
        rootLayout.addView(extPresetSpinner)

        val extensionsEdit = EditText(this).apply {
            hint = "GL_EXTENSIONS (space-separated list)"
            setText(config.extensions)
            minLines = 2
            maxLines = 4
        }
        rootLayout.addView(extensionsEdit)

        gpuPresetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, pos: Int, id: Long) {
                when (pos) {
                    1 -> {
                        rendererEdit.setText("Adreno (TM) 750")
                        vendorEdit.setText("Qualcomm")
                        versionEdit.setText("OpenGL ES 3.2 V@0762.0 (GIT@I918b5f8e1a)")
                    }
                    2 -> {
                        rendererEdit.setText("Adreno (TM) 740")
                        vendorEdit.setText("Qualcomm")
                        versionEdit.setText("OpenGL ES 3.2 V@0676.0 (GIT@b83c2a1)")
                    }
                    3 -> {
                        rendererEdit.setText("Immortalis-G720 MC12")
                        vendorEdit.setText("ARM")
                        versionEdit.setText("OpenGL ES 3.2 v1.r46p0-01eac0")
                    }
                    4 -> {
                        rendererEdit.setText("Apple A17 Pro GPU")
                        vendorEdit.setText("Apple Inc.")
                        versionEdit.setText("OpenGL ES 3.2 Apple A17 Pro-180.1")
                    }
                    5 -> {
                        rendererEdit.setText("NVIDIA Tegra X1")
                        vendorEdit.setText("NVIDIA Corporation")
                        versionEdit.setText("OpenGL ES 3.2 NVIDIA 510.00")
                    }
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        extPresetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, pos: Int, id: Long) {
                when (pos) {
                    1 -> extensionsEdit.setText("GL_OES_EGL_image GL_OES_EGL_image_external GL_OES_H_texture_float GL_OES_texture_half_float GL_OES_vertex_half_float GL_EXT_color_buffer_float GL_EXT_color_buffer_half_float GL_KHR_texture_compression_astc_ldr GL_KHR_texture_compression_astc_hdr GL_EXT_texture_filter_anisotropic GL_OVR_multiview GL_OVR_multiview2 GL_QCOM_TILED_RENDERING")
                    2 -> extensionsEdit.setText("GL_OES_EGL_image GL_OES_EGL_image_external GL_OES_texture_float GL_OES_texture_half_float GL_EXT_color_buffer_float GL_KHR_texture_compression_astc_ldr GL_KHR_texture_compression_astc_hdr GL_ARM_shader_framebuffer_fetch GL_EXT_texture_filter_anisotropic")
                    3 -> extensionsEdit.setText("GL_OES_EGL_image GL_OES_texture_float GL_OES_texture_half_float GL_EXT_texture_compression_s3tc GL_EXT_texture_compression_dxt1 GL_NV_texture_compression_s3tc_update GL_EXT_texture_filter_anisotropic GL_KHR_texture_compression_astc_ldr")
                }
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }

        // -------------------------------------------------------------
        // 5. Shaders & Custom GLSL
        // -------------------------------------------------------------
        addSectionHeader(rootLayout, "4. GLSL Shaders & Post-Processing")

        val shaderSpinner = Spinner(this)
        val shaderOptions = arrayOf(
            "None",
            "DLSS 5 Ultra",
            "Vibrant HDR",
            "Golden Hour",
            "ACES Filmic",
            "Cyberpunk Neon",
            "Cel-Shading",
            "Night Vision",
            "Custom GLSL"
        )
        shaderSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, shaderOptions)
        val curShaderIdx = shaderOptions.indexOf(config.shaderPreset).coerceAtLeast(0)
        shaderSpinner.setSelection(curShaderIdx)
        rootLayout.addView(shaderSpinner)

        val customGlslEdit = EditText(this).apply {
            hint = "Custom GLSL (e.g. 4K Broadcast formula)"
            val defaultPhotoreal = "color.rgb = mix(vec3(dot(color.rgb, vec3(0.2126, 0.7152, 0.0722))), color.rgb, 1.26); color.rgb = color.rgb * vec3(1.04, 1.05, 1.01); color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(0.92)) * 1.12;"
            setText(config.customGlsl.ifEmpty { defaultPhotoreal })
            minLines = 2
        }
        rootLayout.addView(customGlslEdit)

        val fp16Switch = Switch(this).apply {
            text = "Force FP16 (mediump) Shaders for Extra Speed"
            isChecked = config.forceFp16Shaders
            setOnCheckedChangeListener { _, isChecked -> config.forceFp16Shaders = isChecked }
        }
        rootLayout.addView(fp16Switch)

        // -------------------------------------------------------------
        // 6. Frame Generation, Resolution & Textures
        // -------------------------------------------------------------
        addSectionHeader(rootLayout, "5. Frame Rate & Resolution Scaling")

        val fgLabel = TextView(this).apply {
            text = "Frame Rate / VSync Mode:"
            setPadding(0, 4, 0, 4)
        }
        rootLayout.addView(fgLabel)

        val frameGenSpinner = Spinner(this)
        val frameGenOptions = arrayOf("Off", "2x (120 FPS)", "Uncapped")
        frameGenSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, frameGenOptions)
        val curFgIdx = frameGenOptions.indexOf(config.frameGenMode).coerceAtLeast(0)
        frameGenSpinner.setSelection(curFgIdx)
        rootLayout.addView(frameGenSpinner)

        val resScalePercent = (config.resolutionScale * 100f).roundToInt().coerceIn(25, 100)
        val resLabel = TextView(this).apply {
            text = "Render Resolution Scale: $resScalePercent%"
            setPadding(0, 12, 0, 4)
        }
        rootLayout.addView(resLabel)

        val resSeekBar = SeekBar(this).apply {
            max = 100
            progress = resScalePercent
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                    val clamped = p.coerceIn(25, 100)
                    resLabel.text = "Render Resolution Scale: $clamped%"
                    config.resolutionScale = clamped / 100f
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}
                override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }
        rootLayout.addView(resSeekBar)

        val tex16Switch = Switch(this).apply {
            text = "Optimize Linear Texture Filtering"
            isChecked = config.force16BitTextures
            setOnCheckedChangeListener { _, isChecked -> config.force16BitTextures = isChecked }
            setPadding(0, 8, 0, 24)
        }
        rootLayout.addView(tex16Switch)

        // -------------------------------------------------------------
        // 7. Save & Apply Button
        // -------------------------------------------------------------
        val saveBtn = Button(this).apply {
            text = "Save & Apply Settings"
            setOnClickListener {
                config.fakeCpuName = cpuNameEdit.text.toString().trim()
                config.fakeCpuHardware = cpuHwEdit.text.toString().trim()
                config.renderer = rendererEdit.text.toString().trim()
                config.vendor = vendorEdit.text.toString().trim()
                config.version = versionEdit.text.toString().trim()
                config.extensions = extensionsEdit.text.toString().trim()
                config.shaderPreset = shaderSpinner.selectedItem.toString()
                config.customGlsl = customGlslEdit.text.toString().trim()
                config.frameGenMode = frameGenSpinner.selectedItem.toString()

                when (glesSpinner.selectedItemPosition) {
                    1 -> { config.targetGlMajor = 3; config.targetGlMinor = 2 }
                    2 -> { config.targetGlMajor = 3; config.targetGlMinor = 1 }
                    3 -> { config.targetGlMajor = 3; config.targetGlMinor = 0 }
                    4 -> { config.targetGlMajor = 2; config.targetGlMinor = 0 }
                    else -> { config.targetGlMajor = 0; config.targetGlMinor = 0 }
                }

                val ok = RootConfigManager.saveConfig(config)
                if (ok) {
                    Toast.makeText(this@AppDetailActivity, "Saved! Force-stop & relaunch $packageName", Toast.LENGTH_SHORT).show()
                    finish()
                } else {
                    Toast.makeText(this@AppDetailActivity, "Root permission failed!", Toast.LENGTH_LONG).show()
                }
            }
        }
        rootLayout.addView(saveBtn)
    }
}
