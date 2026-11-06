package com.gltools.zygisk

import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity

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

    private fun setupViews() {
        val rootLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
        }

        val scrollView = ScrollView(this).apply {
            addView(rootLayout)
        }
        setContentView(scrollView)

        // 1. Header & Master Enable
        val titleText = TextView(this).apply {
            text = "GLTools Settings: $packageName"
            textSize = 20f
            setPadding(0, 0, 0, 16)
        }
        rootLayout.addView(titleText)

        val enableSwitch = Switch(this).apply {
            text = "Enable GLTools for this app"
            isChecked = config.enabled
            setOnCheckedChangeListener { _, isChecked -> config.enabled = isChecked }
            setPadding(0, 0, 0, 24)
        }
        rootLayout.addView(enableSwitch)

        // -------------------------------------------------------------
        // 2. Fake CPU & Cores Section (GLTools Feature)
        // -------------------------------------------------------------
        val cpuHeader = TextView(this).apply {
            text = "CPU & Processor Spoofing"
            textSize = 18f
            setPadding(0, 16, 0, 8)
        }
        rootLayout.addView(cpuHeader)

        val fakeCpuSwitch = Switch(this).apply {
            text = "Emulate Fake CPU & Cores"
            isChecked = config.fakeCpuEnabled
            setOnCheckedChangeListener { _, isChecked -> config.fakeCpuEnabled = isChecked }
        }
        rootLayout.addView(fakeCpuSwitch)

        val cpuPresetSpinner = Spinner(this)
        val cpuPresets = arrayOf(
            "None",
            "Snapdragon 8 Gen 3 (8 Cores)",
            "Snapdragon 8 Gen 2 (8 Cores)",
            "Dimensity 9300 (8 Cores)",
            "Apple A17 Pro (6 Cores)",
            "Tensor G3 (9 Cores)",
            "Custom"
        )
        cpuPresetSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, cpuPresets)
        rootLayout.addView(cpuPresetSpinner)

        val cpuNameEdit = EditText(this).apply {
            hint = "Processor Name (e.g. Qualcomm Snapdragon 8 Gen 3)"
            setText(config.fakeCpuName)
        }
        rootLayout.addView(cpuNameEdit)

        val cpuHwEdit = EditText(this).apply {
            hint = "Hardware String (e.g. Qualcomm Technologies, Inc SM8650)"
            setText(config.fakeCpuHardware)
        }
        rootLayout.addView(cpuHwEdit)

        val coresLabel = TextView(this).apply {
            text = "CPU Cores: ${config.fakeCpuCores}"
            setPadding(0, 8, 0, 4)
        }
        rootLayout.addView(coresLabel)

        val coresSeekBar = SeekBar(this).apply {
            max = 16
            progress = if (config.fakeCpuCores in 1..16) config.fakeCpuCores else 8
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: SeekBar?, p: Int, fromUser: Boolean) {
                    val count = if (p < 1) 1 else p
                    coresLabel.text = "CPU Cores: $count"
                    config.fakeCpuCores = count
                }
                override fun onStartTrackingTouch(sb: SeekBar?) {}
                override fun onStopTrackingTouch(sb: SeekBar?) {}
            })
        }
        rootLayout.addView(coresSeekBar)

        cpuPresetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p0: AdapterView<*>?, p1: android.view.View?, pos: Int, id: Long) {
                when (pos) {
                    1 -> { // 8 Gen 3
                        cpuNameEdit.setText("Qualcomm Snapdragon 8 Gen 3")
                        cpuHwEdit.setText("Qualcomm Technologies, Inc SM8650")
                        config.fakeCpuPlatform = "pineapple"
                        coresSeekBar.progress = 8
                    }
                    2 -> { // 8 Gen 2
                        cpuNameEdit.setText("Qualcomm Snapdragon 8 Gen 2")
                        cpuHwEdit.setText("Qualcomm Technologies, Inc SM8550")
                        config.fakeCpuPlatform = "kalama"
                        coresSeekBar.progress = 8
                    }
                    3 -> { // Dimensity 9300
                        cpuNameEdit.setText("MediaTek Dimensity 9300")
                        cpuHwEdit.setText("MT6989")
                        config.fakeCpuPlatform = "mt6989"
                        coresSeekBar.progress = 8
                    }
                    4 -> { // A17 Pro
                        cpuNameEdit.setText("Apple A17 Pro")
                        cpuHwEdit.setText("Apple A17 Pro")
                        config.fakeCpuPlatform = "t8130"
                        coresSeekBar.progress = 6
                    }
                    5 -> { // Tensor G3
                        cpuNameEdit.setText("Google Tensor G3")
                        cpuHwEdit.setText("Tensor G3")
                        config.fakeCpuPlatform = "zuma"
                        coresSeekBar.progress = 9
                    }
                }
            }
            override fun onNothingSelected(p0: AdapterView<*>?) {}
        }

        // -------------------------------------------------------------
        // 3. GPU Spoofing Section
        // -------------------------------------------------------------
        val gpuHeader = TextView(this).apply {
            text = "GPU Spoofing"
            textSize = 18f
            setPadding(0, 24, 0, 8)
        }
        rootLayout.addView(gpuHeader)

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
            hint = "GL_VERSION (e.g. OpenGL ES 3.2 V@0615.0)"
            setText(config.version)
        }
        rootLayout.addView(versionEdit)

        // -------------------------------------------------------------
        // 4. Shader & Frame Generation
        // -------------------------------------------------------------
        val shaderHeader = TextView(this).apply {
            text = "Shaders & Frame Generation"
            textSize = 18f
            setPadding(0, 24, 0, 8)
        }
        rootLayout.addView(shaderHeader)

        val shaderSpinner = Spinner(this)
        val shaderOptions = arrayOf("None", "DLSS 5 Ultra", "Vibrant HDR", "Golden Hour", "ACES Filmic", "Cyberpunk Neon", "Cel-Shading", "Custom GLSL")
        shaderSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, shaderOptions)
        val curShaderIdx = shaderOptions.indexOf(config.shaderPreset).coerceAtLeast(0)
        shaderSpinner.setSelection(curShaderIdx)
        rootLayout.addView(shaderSpinner)

        val customGlslEdit = EditText(this).apply {
            hint = "Custom GLSL (e.g. color.rgb = pow(color.rgb, vec3(0.92)) * 1.12;)"
            setText(config.customGlsl)
        }
        rootLayout.addView(customGlslEdit)

        val frameGenSpinner = Spinner(this)
        val frameGenOptions = arrayOf("Off", "2x (120 FPS)", "Uncapped")
        frameGenSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, frameGenOptions)
        val curFgIdx = frameGenOptions.indexOf(config.frameGenMode).coerceAtLeast(0)
        frameGenSpinner.setSelection(curFgIdx)
        rootLayout.addView(frameGenSpinner)

        // -------------------------------------------------------------
        // 5. Save Button
        // -------------------------------------------------------------
        val saveBtn = Button(this).apply {
            text = "Save & Apply Settings"
            setOnClickListener {
                config.fakeCpuName = cpuNameEdit.text.toString()
                config.fakeCpuHardware = cpuHwEdit.text.toString()
                config.renderer = rendererEdit.text.toString()
                config.vendor = vendorEdit.text.toString()
                config.version = versionEdit.text.toString()
                config.shaderPreset = shaderSpinner.selectedItem.toString()
                config.customGlsl = customGlslEdit.text.toString()
                config.frameGenMode = frameGenSpinner.selectedItem.toString()

                val ok = RootConfigManager.saveConfig(config)
                if (ok) {
                    Toast.makeText(this@AppDetailActivity, "Settings Saved Successfully!", Toast.LENGTH_SHORT).show()
                    finish()
                } else {
                    Toast.makeText(this@AppDetailActivity, "Root permission failed!", Toast.LENGTH_LONG).show()
                }
            }
        }
        rootLayout.addView(saveBtn)
    }
}
