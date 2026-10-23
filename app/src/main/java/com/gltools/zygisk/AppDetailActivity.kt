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

class AppDetailActivity : AppCompatActivity() {

    private lateinit var config: GameConfig

    private lateinit var switchActive: SwitchCompat
    private lateinit var actvPreset: AutoCompleteTextView
    private lateinit var etVendor: TextInputEditText
    private lateinit var etRenderer: TextInputEditText
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
        etVendor = findViewById(R.id.etVendor)
        etRenderer = findViewById(R.id.etRenderer)
        sbResolution = findViewById(R.id.sbResolution)
        tvResolutionValue = findViewById(R.id.tvResolutionValue)
        switchTextures = findViewById(R.id.switchTextures)
        btnSave = findViewById(R.id.btnSave)
    }

    private fun setupListeners() {
        val presetKeys = GameConfig.PRESETS.keys.toList()
        val adapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, presetKeys)
        actvPreset.setAdapter(adapter)

        actvPreset.setOnItemClickListener { _, _, position, _ ->
            val key = presetKeys[position]
            GameConfig.PRESETS[key]?.let { pair ->
                etVendor.setText(pair.first)
                etRenderer.setText(pair.second)
            }
        }

        sbResolution.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val scale = 0.5f + (progress / 100f) * 0.5f // 50% to 100%
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
        switchActive.isChecked = config.enabled
        etVendor.setText(config.vendor)
        etRenderer.setText(config.renderer)
        switchTextures.isChecked = config.optimizeTextures

        // Calculate progress for resolution (0.5 to 1.0)
        val progress = (((config.downscaleFactor.coerceIn(0.5f, 1.0f) - 0.5f) / 0.5f) * 100).toInt()
        sbResolution.progress = progress
        tvResolutionValue.text = String.format("%.0f%% (Factor: %.2fx)", config.downscaleFactor * 100, config.downscaleFactor)
    }

    private fun saveChanges() {
        config.enabled = switchActive.isChecked
        config.vendor = etVendor.text.toString().trim()
        config.renderer = etRenderer.text.toString().trim()
        config.optimizeTextures = switchTextures.isChecked
        config.downscaleFactor = 0.5f + (sbResolution.progress / 100f) * 0.5f

        val success = RootConfigManager.saveConfig(config)
        if (success) {
            Toast.makeText(this, "Profile saved and applied to Zygisk!", Toast.LENGTH_SHORT).show()
            finish()
        } else {
            Toast.makeText(this, "Failed to write config. Ensure root is granted.", Toast.LENGTH_LONG).show()
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        finish()
        return true
    }
}
