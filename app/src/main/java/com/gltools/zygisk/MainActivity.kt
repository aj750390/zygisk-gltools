package com.gltools.zygisk

import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
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

class MainActivity : AppCompatActivity() {

    private lateinit var rvApps: RecyclerView
    private lateinit var btnToggleHud: MaterialButton
    private lateinit var adapter: AppListAdapter
    private var isOverlayRunning = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        rvApps = findViewById(R.id.rvApps)
        btnToggleHud = findViewById(R.id.btnToggleHud)

        rvApps.layoutManager = LinearLayoutManager(this)

        checkRootStatus()
        loadInstalledApps()

        btnToggleHud.setOnClickListener {
            toggleOverlayService()
        }
    }

    private fun checkRootStatus() {
        if (!RootConfigManager.isRootAvailable()) {
            Toast.makeText(this, "Root access not detected! Module features require Magisk/KernelSU.", Toast.LENGTH_LONG).show()
        }
    }

    private fun loadInstalledApps() {
        val pm = packageManager
        val packages = pm.getInstalledApplications(PackageManager.GET_META_DATA)
        val appList = mutableListOf<GameConfig>()

        for (app in packages) {
            // Filter non-system apps or games
            val isSystem = (app.flags and ApplicationInfo.FLAG_SYSTEM) != 0
            if (!isSystem || (app.category == ApplicationInfo.CATEGORY_GAME)) {
                val appName = pm.getApplicationLabel(app).toString()
                val config = RootConfigManager.loadConfig(app.packageName, appName)
                appList.add(config)
            }
        }

        appList.sortBy { it.appName.lowercase() }
        adapter = AppListAdapter(appList) { config ->
            val intent = Intent(this, AppDetailActivity::class.java).apply {
                putExtra("EXTRA_CONFIG", config)
            }
            startActivity(intent)
        }
        rvApps.adapter = adapter
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
            btnToggleHud.text = "Stop Floating HUD"
            isOverlayRunning = true
        } else {
            stopService(serviceIntent)
            btnToggleHud.text = "Start Floating HUD"
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
            holder.switchEnable.isChecked = item.enabled

            try {
                val icon = packageManager.getApplicationIcon(item.packageName)
                holder.ivIcon.setImageDrawable(icon)
            } catch (e: Exception) {
                holder.ivIcon.setImageResource(android.R.drawable.sym_def_app_icon)
            }

            holder.switchEnable.setOnCheckedChangeListener { _, isChecked ->
                item.enabled = isChecked
                RootConfigManager.saveConfig(item)
            }

            holder.itemView.setOnClickListener { onItemClick(item) }
        }

        override fun getItemCount() = items.size
    }
}
