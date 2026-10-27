package com.gltools.zygisk

import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.AutoCompleteTextView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat
import com.gltools.zygisk.model.GameConfig
import com.gltools.zygisk.util.RootConfigManager
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class AppDetailActivity : AppCompatActivity() {

    private lateinit var config: GameConfig

    private lateinit var switchActive: SwitchCompat
    private lateinit var actvFrameGenMode: AutoCompleteTextView
    private lateinit var actvShaderPreset: AutoCompleteTextView
    private lateinit var etCustomGlsl: TextInputEditText
    private lateinit var switchFp16Shaders: SwitchCompat
    private lateinit var actvPreset: AutoCompleteTextView
    private lateinit var actvGlVersionPreset: AutoCompleteTextView
    private lateinit var etVersion: TextInputEditText
    private lateinit var actvExtensionsPreset: AutoCompleteTextView
    private lateinit var etExtensions: TextInputEditText
    private lateinit var etVendor: TextInputEditText
    private lateinit var etRenderer: TextInputEditText
    private lateinit var etModel: TextInputEditText
    private lateinit var sbResolution: SeekBar
    private lateinit var tvResolutionValue: TextView
    private lateinit var switchTextures: SwitchCompat
    private lateinit var btnSave: MaterialButton

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_app_detail)

        config = intent.getSerializableExtra("EXTRA_CONFIG") as? GameConfig
            ?: return finish()

        supportActionBar?.title = config.appName
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        bindViews()
        setupListeners()
        populateUI()
    }

    private fun bindViews() {
        switchActive = findViewById(R.id.switchActive)
        actvFrameGenMode = findViewById(R.id.actvFrameGenMode)
        actvShaderPreset = findViewById(R.id.actvShaderPreset)
        etCustomGlsl = findViewById(R.id.etCustomGlsl)
        switchFp16Shaders = findViewById(R.id.switchFp16Shaders)
        actvPreset = findViewById(R.id.actvPreset)
        actvGlVersionPreset = findViewById(R.id.actvGlVersionPreset)
        etVersion = findViewById(R.id.etVersion)
        actvExtensionsPreset = findViewById(R.id.actvExtensionsPreset)
        etExtensions = findViewById(R.id.etExtensions)
        etVendor = findViewById(R.id.etVendor)
        etRenderer = findViewById(R.id.etRenderer)
        etModel = findViewById(R.id.etModel)
        sbResolution = findViewById(R.id.sbResolution)
        tvResolutionValue = findViewById(R.id.tvResolutionValue)
        switchTextures = findViewById(R.id.switchTextures)
        btnSave = findViewById(R.id.btnSave)
    }

    private fun setupListeners() {
        val frameGenList = GameConfig.FRAME_GEN_PRESETS
        val frameGenAdapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, frameGenList)
        actvFrameGenMode.setAdapter(frameGenAdapter)

        actvFrameGenMode.setOnItemClickListener { _, _, position, _ ->
            config.frameGenMode = frameGenList[position]
            switchActive.isChecked = true
        }

        val shaderList = GameConfig.SHADER_PRESETS
        val shaderAdapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, shaderList)
        actvShaderPreset.setAdapter(shaderAdapter)

        actvShaderPreset.setOnItemClickListener { _, _, position, _ ->
            val selectedShader = shaderList[position]
            config.shaderPreset = selectedShader
            if (selectedShader.contains("Custom GLSL") && etCustomGlsl.text.isNullOrBlank()) {
                etCustomGlsl.setText("color.rgb = pow(color.rgb, vec3(0.85)) * 1.18;")
            }
            switchActive.isChecked = true
        }

        val extPresetKeys = GameConfig.GL_EXTENSIONS_PRESETS.keys.toList()
        val extPresetAdapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, extPresetKeys)
        actvExtensionsPreset.setAdapter(extPresetAdapter)

        actvExtensionsPreset.setOnItemClickListener { _, _, position, _ ->
            val extKey = extPresetKeys[position]
            config.extensionsPreset = extKey
            val extVal = GameConfig.GL_EXTENSIONS_PRESETS[extKey].orEmpty()
            config.extensions = extVal
            etExtensions.setText(extVal)
            switchActive.isChecked = true
        }

        val presetKeys = GameConfig.PRESETS.keys.toList()
        val presetAdapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, presetKeys)
        actvPreset.setAdapter(presetAdapter)

        val glVersionList = GameConfig.GL_VERSION_PRESETS
        val glVersionAdapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, glVersionList)
        actvGlVersionPreset.setAdapter(glVersionAdapter)

        actvPreset.setOnItemClickListener { _, _, position, _ ->
            val key = presetKeys[position]
            GameConfig.PRESETS[key]?.let { preset ->
                config.presetName = key
                config.vendor = preset.vendor
                config.renderer = preset.renderer
                config.version = preset.version
                config.extensions = preset.extensions
                config.model = preset.model
                config.manufacturer = preset.manufacturer
                config.brand = preset.brand
                config.device = preset.device
                config.board = preset.board
                config.hardware = preset.hardware

                etVendor.setText(preset.vendor)
                etRenderer.setText(preset.renderer)
                etVersion.setText(preset.version)
                actvGlVersionPreset.setText(preset.version, false)
                etExtensions.setText(preset.extensions)
                etModel.setText(preset.model)
                switchActive.isChecked = true
            }
        }

        actvGlVersionPreset.setOnItemClickListener { _, _, position, _ ->
            val selectedVersion = glVersionList[position]
            config.version = selectedVersion
            etVersion.setText(selectedVersion)
            switchActive.isChecked = true
        }

        sbResolution.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val scale = 0.5f + (progress / 100f) * 0.5f
                tvResolutionValue.text = String.format("%.0f%% (Factor: %.2fx)", scale * 100, scale)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        btnSave.setOnClickListener {
            saveChanges()
        }
    }

    private fun populateUI() {
        switchActive.isChecked = true
        actvFrameGenMode.setText(config.frameGenMode, false)
        actvShaderPreset.setText(config.shaderPreset, false)
        etCustomGlsl.setText(config.customGlsl)
        switchFp16Shaders.isChecked = config.forceFp16Shaders
        actvPreset.setText(config.presetName, false)
        actvGlVersionPreset.setText(config.version, false)
        etVersion.setText(config.version)
        actvExtensionsPreset.setText(config.extensionsPreset, false)
        etExtensions.setText(config.extensions)
        etVendor.setText(config.vendor)
        etRenderer.setText(config.renderer)
        etModel.setText(config.model)
        switchTextures.isChecked = config.optimizeTextures

        val progress = (((config.downscaleFactor.coerceIn(0.5f, 1.0f) - 0.5f) / 0.5f) * 100).toInt()
        sbResolution.progress = progress
        tvResolutionValue.text = String.format("%.0f%% (Factor: %.2fx)", config.downscaleFactor * 100, config.downscaleFactor)
    }

    private fun saveChanges() {
        config.enabled = switchActive.isChecked
        config.frameGenMode = actvFrameGenMode.text?.toString()?.trim().orEmpty().ifEmpty { "Off (Native Framerate)" }
        config.shaderPreset = actvShaderPreset.text?.toString()?.trim().orEmpty().ifEmpty { "None (Default Game Shaders)" }
        config.customGlsl = etCustomGlsl.text?.toString()?.trim().orEmpty()
        config.forceFp16Shaders = switchFp16Shaders.isChecked
        config.version = etVersion.text?.toString()?.trim().orEmpty().ifEmpty { "OpenGL ES 3.2 V@0615.0 (GLTools)" }
        config.extensionsPreset = actvExtensionsPreset.text?.toString()?.trim().orEmpty().ifEmpty { "Adreno 750 Flagship Extensions" }
        config.extensions = etExtensions.text?.toString()?.trim().orEmpty()
        config.vendor = etVendor.text?.toString()?.trim().orEmpty().ifEmpty { "Qualcomm" }
        config.renderer = etRenderer.text?.toString()?.trim().orEmpty().ifEmpty { "Adreno (TM) 750" }
        config.model = etModel.text?.toString()?.trim().orEmpty().ifEmpty { "SM-S928B" }
        config.optimizeTextures = switchTextures.isChecked
        config.downscaleFactor = 0.5f + (sbResolution.progress / 100f) * 0.5f

        btnSave.isEnabled = false
        CoroutineScope(Dispatchers.IO).launch {
            val success = RootConfigManager.saveConfig(config)
            withContext(Dispatchers.Main) {
                btnSave.isEnabled = true
                if (success) {
                    Toast.makeText(
                        this@AppDetailActivity,
                        "Saved! Force-stop & reopen ${config.appName} to apply.",
                        Toast.LENGTH_LONG
                    ).show()
                    finish()
                } else {
                    Toast.makeText(
                        this@AppDetailActivity,
                        "Failed to write config. Ensure root access is granted.",
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
