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
    private lateinit var actvPreset: AutoCompleteTextView
    private lateinit var actvGlVersionPreset: AutoCompleteTextView
    private lateinit var etVersion: TextInputEditText
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
        actvPreset = findViewById(R.id.actvPreset)
        actvGlVersionPreset = findViewById(R.id.actvGlVersionPreset)
        etVersion = findViewById(R.id.etVersion)
        etVendor = findViewById(R.id.etVendor)
        etRenderer = findViewById(R.id.etRenderer)
        etModel = findViewById(R.id.etModel)
        sbResolution = findViewById(R.id.sbResolution)
        tvResolutionValue = findViewById(R.id.tvResolutionValue)
        switchTextures = findViewById(R.id.switchTextures)
        btnSave = findViewById(R.id.btnSave)
    }

    private fun setupListeners() {
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
        // Default the switch to ON when opening detail screen so saving immediately activates the profile
        switchActive.isChecked = if (config.enabled) true else true
        actvPreset.setText(config.presetName, false)
        actvGlVersionPreset.setText(config.version, false)
        etVersion.setText(config.version)
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
        config.version = etVersion.text?.toString()?.trim().orEmpty().ifEmpty { "OpenGL ES 3.2 V@0615.0 (GLTools)" }
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
