package com.gltools.zygisk

import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var appListView: ListView
    private val appList = mutableListOf<AppItem>()

    data class AppItem(val name: String, val packageName: String, val isConfigured: Boolean)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
        }
        setContentView(layout)

        // Device Info Card
        val cpuCores = Runtime.getRuntime().availableProcessors()
        val cpuModel = getCpuModel()
        val header = TextView(this).apply {
            text = "Zygisk GLTools\nActive CPU: $cpuModel ($cpuCores Cores)"
            textSize = 18f
            setPadding(0, 0, 0, 16)
        }
        layout.addView(header)

        appListView = ListView(this)
        layout.addView(appListView)

        loadInstalledApps()
    }

    override fun onResume() {
        super.onResume()
        loadInstalledApps()
    }

    private fun loadInstalledApps() {
        appList.clear()
        val pm = packageManager
        val installed = pm.getInstalledApplications(PackageManager.GET_META_DATA)
        val configDir = File("/data/adb/gltools/packages")

        for (app in installed) {
            if ((app.flags and ApplicationInfo.FLAG_SYSTEM) == 0 || app.packageName.contains("game")) {
                val name = pm.getApplicationLabel(app).toString()
                val isConf = File(configDir, "${app.packageName}.conf").exists()
                appList.add(AppItem(name, app.packageName, isConf))
            }
        }

        val adapter = object : ArrayAdapter<AppItem>(this, android.R.layout.simple_list_item_2, android.R.id.text1, appList) {
            override fun getView(position: Int, convertView: android.view.View?, parent: android.view.ViewGroup): android.view.View {
                val view = super.getView(position, convertView, parent)
                val item = getItem(position)
                val text1 = view.findViewById<TextView>(android.R.id.text1)
                val text2 = view.findViewById<TextView>(android.R.id.text2)
                text1.text = item?.name + if (item?.isConfigured == true) " [Hooked]" else ""
                text2.text = item?.packageName
                return view
            }
        }

        appListView.adapter = adapter
        appListView.setOnItemClickListener { _, _, pos, _ ->
            val item = appList[pos]
            val intent = Intent(this, AppDetailActivity::class.java).apply {
                putExtra("EXTRA_PACKAGE_NAME", item.packageName)
            }
            startActivity(intent)
        }
    }

    private fun getCpuModel(): String {
        try {
            File("/proc/cpuinfo").forEachLine { line ->
                if (line.startsWith("Hardware") || line.startsWith("model name")) {
                    val parts = line.split(":", limit = 2)
                    if (parts.size == 2) return parts[1].trim()
                }
            }
        } catch (_: Exception) {}
        return android.os.Build.HARDWARE
    }
}
