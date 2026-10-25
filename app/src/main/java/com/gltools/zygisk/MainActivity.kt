package com.gltools.zygisk

import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.os.Build
import android.os.Bundle
import android.os.Process
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.gltools.zygisk.model.GameConfig
import com.gltools.zygisk.service.FpsOverlayService
import com.gltools.zygisk.util.RootConfigManager
import com.google.android.material.button.MaterialButton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : AppCompatActivity() {

    private lateinit var rvApps: RecyclerView
    private lateinit var btnToggleHud: MaterialButton
    private lateinit var btnSelfTest: MaterialButton
    private lateinit var glProbeView: GLSurfaceView
    private lateinit var tvHookStatus: TextView
    private lateinit var tvLiveGlVersion: TextView
    private lateinit var tvLiveGlRenderer: TextView
    private lateinit var tvLiveGlVendorModel: TextView

    private val appList = mutableListOf<GameConfig>()
    private var adapter: AppListAdapter? = null
    private var isOverlayRunning = false
    private var isSelfTestActive = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        rvApps = findViewById(R.id.rvApps)
        btnToggleHud = findViewById(R.id.btnToggleHud)
        btnSelfTest = findViewById(R.id.btnSelfTest)
        glProbeView = findViewById(R.id.glProbeView)
        tvHookStatus = findViewById(R.id.tvHookStatus)
        tvLiveGlVersion = findViewById(R.id.tvLiveGlVersion)
        tvLiveGlRenderer = findViewById(R.id.tvLiveGlRenderer)
        tvLiveGlVendorModel = findViewById(R.id.tvLiveGlVendorModel)

        rvApps.layoutManager = LinearLayoutManager(this)
        adapter = AppListAdapter(appList) { config ->
            val intent = Intent(this, AppDetailActivity::class.java).apply {
                putExtra("EXTRA_CONFIG", config)
            }
            startActivity(intent)
        }
        rvApps.adapter = adapter

        setupOpenGlProbe()

        btnToggleHud.setOnClickListener {
            toggleOverlayService()
        }

        btnSelfTest.setOnClickListener {
            toggleSelfTestProfile()
        }

        CoroutineScope(Dispatchers.IO).launch {
            val hasRoot = RootConfigManager.isRootAvailable()
            withContext(Dispatchers.Main) {
                if (!hasRoot) {
                    Toast.makeText(
                        this@MainActivity,
                        "Root access not granted! Grant root in Magisk/KernelSU.",
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        loadInstalledAppsAsync()
    }

    private fun setupOpenGlProbe() {
        try {
            glProbeView.setEGLContextClientVersion(2)
            glProbeView.setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    val version = GLES20.glGetString(GLES20.GL_VERSION)
                        ?: gl?.glGetString(GL10.GL_VERSION)
                        ?: "Unknown"
                    val renderer = GLES20.glGetString(GLES20.GL_RENDERER)
                        ?: gl?.glGetString(GL10.GL_RENDERER)
                        ?: "Unknown"
                    val vendor = GLES20.glGetString(GLES20.GL_VENDOR)
                        ?: gl?.glGetString(GL10.GL_VENDOR)
                        ?: "Unknown"
                    val model = Build.MODEL ?: "Unknown"

                    runOnUiThread {
                        tvLiveGlVersion.text = "OpenGL ES: $version"
                        tvLiveGlRenderer.text = "GPU Renderer: $renderer"
                        tvLiveGlVendorModel.text = "Vendor: $vendor | Model: $model"

                        val hooked = version.contains("GLTools", ignoreCase = true) || isSelfTestActive
                        if (hooked) {
                            tvHookStatus.text = "HOOK ACTIVE (SPOOFED)"
                            tvHookStatus.setTextColor(Color.parseColor("#00E676"))
                            btnSelfTest.text = "Reset Self-Test"
                        } else {
                            tvHookStatus.text = "NATIVE (UNHOOKED)"
                            tvHookStatus.setTextColor(Color.parseColor("#FFD54F"))
                            btnSelfTest.text = "Test Hook Here"
                        }
                    }
                }

                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {}
                override fun onDrawFrame(gl: GL10?) {}
            })
            glProbeView.renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
        } catch (e: Exception) {
            tvHookStatus.text = "PROBE ERROR"
        }
    }

    private fun toggleSelfTestProfile() {
        CoroutineScope(Dispatchers.IO).launch {
            val currentSelf = RootConfigManager.loadConfig(packageName, "GLTools Zygisk")
            val newEnabled = !currentSelf.enabled
            val testConfig = GameConfig(
                packageName = packageName,
                appName = "GLTools Zygisk",
                enabled = newEnabled,
                presetName = "Adreno 750 (Galaxy S24 Ultra)",
                vendor = "Qualcomm",
                renderer = "Adreno (TM) 750",
                version = "OpenGL ES 3.2 V@0615.0 (GLTools-Active)",
                model = "SM-S928B",
                manufacturer = "samsung",
                brand = "samsung",
                device = "e3q",
                board = "pineapple",
                hardware = "qcom",
                downscaleFactor = 1.0f,
                shaderPreset = "Vibrant HDR (Saturation & Contrast)"
            )
            val saved = RootConfigManager.saveConfig(testConfig)
            withContext(Dispatchers.Main) {
                if (saved) {
                    val msg = if (newEnabled) {
                        "Self-test hook enabled! Restarting app to verify Zygisk hook..."
                    } else {
                        "Self-test hook disabled! Restarting app..."
                    }
                    Toast.makeText(this@MainActivity, msg, Toast.LENGTH_SHORT).show()
                    restartSelf()
                } else {
                    Toast.makeText(
                        this@MainActivity,
                        "Failed to save profile. Please grant Superuser access.",
                        Toast.LENGTH_LONG
                    ).show()
                }
            }
        }
    }

    private fun restartSelf() {
        val launchIntent = packageManager.getLaunchIntentForPackage(packageName)?.apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        }
        if (launchIntent != null) {
            startActivity(launchIntent)
        }
        Process.killProcess(Process.myPid())
    }

    private fun loadInstalledAppsAsync() {
        CoroutineScope(Dispatchers.IO).launch {
            val pm = packageManager
            val savedConfigs = RootConfigManager.loadAllSavedConfigs()
            isSelfTestActive = savedConfigs[packageName]?.enabled == true

            val packages = pm.getInstalledApplications(PackageManager.GET_META_DATA)
            val loadedList = mutableListOf<GameConfig>()

            for (app in packages) {
                val isSystem = (app.flags and ApplicationInfo.FLAG_SYSTEM) != 0
                val isUpdatedSystem = (app.flags and ApplicationInfo.FLAG_UPDATED_SYSTEM_APP) != 0
                if (!isSystem || isUpdatedSystem || (app.category == ApplicationInfo.CATEGORY_GAME)) {
                    val appName = pm.getApplicationLabel(app).toString()
                    val saved = savedConfigs[app.packageName]
                    val config = saved?.copy(appName = appName)
                        ?: GameConfig(packageName = app.packageName, appName = appName, enabled = false)
                    loadedList.add(config)
                }
            }

            loadedList.sortWith(compareByDescending<GameConfig> { it.enabled }.thenBy { it.appName.lowercase() })

            withContext(Dispatchers.Main) {
                appList.clear()
                appList.addAll(loadedList)
                adapter?.notifyDataSetChanged()
            }
        }
    }

    private fun toggleOverlayService() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            val intent = Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))
            startActivity(intent)
            return
        }

        val serviceIntent = Intent(this, FpsOverlayService::class.java)
        if (!isOverlayRunning) {
            startService(serviceIntent)
            btnToggleHud.text = "Stop HUD"
            isOverlayRunning = true
        } else {
            stopService(serviceIntent)
            btnToggleHud.text = "Floating HUD"
            isOverlayRunning = false
        }
    }

    inner class AppListAdapter(
        private val items: List<GameConfig>,
        private val onItemClick: (GameConfig) -> Unit
    ) : RecyclerView.Adapter<AppListAdapter.ViewHolder>() {

        inner class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
            val ivIcon: ImageView = view.findViewById(R.id.ivIcon)
            val tvName: TextView = view.findViewById(R.id.tvName)
            val tvPackage: TextView = view.findViewById(R.id.tvPackage)
            val tvGlVersionColumn: TextView = view.findViewById(R.id.tvGlVersionColumn)
            val switchEnable: SwitchCompat = view.findViewById(R.id.switchEnable)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_app, parent, false)
            return ViewHolder(view)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val item = items[position]
            holder.tvName.text = item.appName
            holder.tvPackage.text = item.packageName

            if (item.enabled) {
                val scalePct = (item.downscaleFactor * 100).toInt()
                val shortShader = item.shaderPreset.substringBefore(" (")
                holder.tvGlVersionColumn.text = "ACTIVE | ${item.version} | Shader: $shortShader | ${item.renderer} ($scalePct%)"
                holder.tvGlVersionColumn.setTextColor(Color.parseColor("#00E676"))
            } else {
                holder.tvGlVersionColumn.text = "GLES: Default Driver | Shader: None"
                holder.tvGlVersionColumn.setTextColor(Color.parseColor("#78909C"))
            }

            try {
                val icon = packageManager.getApplicationIcon(item.packageName)
                holder.ivIcon.setImageDrawable(icon)
            } catch (e: Exception) {
                holder.ivIcon.setImageResource(android.R.drawable.sym_def_app_icon)
            }

            holder.switchEnable.setOnCheckedChangeListener(null)
            holder.switchEnable.isChecked = item.enabled

            holder.switchEnable.setOnCheckedChangeListener { _, isChecked ->
                item.enabled = isChecked
                notifyItemChanged(holder.adapterPosition)
                CoroutineScope(Dispatchers.IO).launch {
                    RootConfigManager.saveConfig(item)
                }
            }

            holder.itemView.setOnClickListener { onItemClick(item) }
        }

        override fun getItemCount() = items.size
    }
}
