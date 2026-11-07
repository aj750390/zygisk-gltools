package com.gltools.zygisk

import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.GLES20
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var appListView: ListView
    private lateinit var inspectorText: TextView
    private val appList = mutableListOf<AppItem>()

    data class AppItem(
        val name: String,
        val packageName: String,
        val config: GameConfig
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val rootLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(28, 28, 28, 28)
        }
        setContentView(rootLayout)

        // Live Hardware & Hook Inspector Card
        inspectorText = TextView(this).apply {
            textSize = 13f
            setTypeface(Typeface.MONOSPACE)
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.parseColor("#1E293B"))
            setPadding(24, 20, 24, 20)
        }
        rootLayout.addView(inspectorText)

        // Action Buttons Row (Self-Test & Overlay)
        val btnRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, 16, 0, 16)
        }

        val selfTestBtn = Button(this).apply {
            text = "Run Hook Self-Test"
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            setOnClickListener { runHookSelfTest() }
        }
        btnRow.addView(selfTestBtn)

        val configureSelfBtn = Button(this).apply {
            text = "Configure Self"
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
            setOnClickListener {
                val intent = Intent(this@MainActivity, AppDetailActivity::class.java).apply {
                    putExtra("EXTRA_PACKAGE_NAME", packageName)
                }
                startActivity(intent)
            }
        }
        btnRow.addView(configureSelfBtn)

        rootLayout.addView(btnRow)

        val listHeader = TextView(this).apply {
            text = "Installed Games & Applications (Tap to Configure)"
            textSize = 15f
            setTypeface(null, Typeface.BOLD)
            setPadding(0, 8, 0, 8)
        }
        rootLayout.addView(listHeader)

        appListView = ListView(this)
        rootLayout.addView(appListView)

        refreshInspectorCard()
        loadInstalledApps()
    }

    override fun onResume() {
        super.onResume()
        refreshInspectorCard()
        loadInstalledApps()
    }

    private fun refreshInspectorCard() {
        val glInfo = queryLiveGlesInfo()
        val cores = Runtime.getRuntime().availableProcessors()
        val cpuModel = getCpuModel()
        val extCount = glInfo.extensions.split(" ").count { it.isNotBlank() }

        inspectorText.text = buildString {
            append("=== LIVE HARDWARE & HOOK INSPECTOR ===\n")
            append("CPU Model  : $cpuModel\n")
            append("CPU Cores  : $cores Cores (Board: ${Build.BOARD} | HW: ${Build.HARDWARE})\n")
            append("GPU Vendor : ${glInfo.vendor}\n")
            append("GPU Render : ${glInfo.renderer}\n")
            append("GL Version : ${glInfo.version}\n")
            append("Extensions : $extCount active extensions")
        }
    }

    private fun runHookSelfTest() {
        val glInfo = queryLiveGlesInfo()
        val cores = Runtime.getRuntime().availableProcessors()
        val cpuModel = getCpuModel()
        val extList = glInfo.extensions.split(" ").filter { it.isNotBlank() }

        val msg = buildString {
            append("CPU / SoC:\n")
            append("• Processor: $cpuModel\n")
            append("• Active Cores (Runtime/sysconf): $cores\n")
            append("• Build.HARDWARE: ${Build.HARDWARE}\n")
            append("• Build.BOARD: ${Build.BOARD}\n\n")
            append("OpenGL ES:\n")
            append("• GL_VENDOR: ${glInfo.vendor}\n")
            append("• GL_RENDERER: ${glInfo.renderer}\n")
            append("• GL_VERSION: ${glInfo.version}\n")
            append("• GL_EXTENSIONS (${extList.size}):\n")
            append(extList.take(18).joinToString(", "))
            if (extList.size > 18) append(" ... (+${extList.size - 18} more)")
        }

        AlertDialog.Builder(this)
            .setTitle("Zygisk GLTools Live Self-Test")
            .setMessage(msg)
            .setPositiveButton("OK", null)
            .show()
    }

    private fun loadInstalledApps() {
        Thread {
            RootConfigManager.refreshAllConfigs()
            val pm = packageManager
            val installed = pm.getInstalledApplications(PackageManager.GET_META_DATA)
            val tempApps = mutableListOf<AppItem>()

            for (app in installed) {
                if ((app.flags and ApplicationInfo.FLAG_SYSTEM) == 0 ||
                    app.packageName.contains("konami") ||
                    app.packageName.contains("game") ||
                    app.packageName == packageName
                ) {
                    val name = pm.getApplicationLabel(app).toString()
                    val cfg = RootConfigManager.getCachedConfig(app.packageName)
                    tempApps.add(AppItem(name, app.packageName, cfg))
                }
            }

            tempApps.sortWith(compareByDescending<AppItem> { it.config.enabled }.thenBy { it.name.lowercase() })

            runOnUiThread {
                appList.clear()
                appList.addAll(tempApps)

                val adapter = object : ArrayAdapter<AppItem>(this@MainActivity, android.R.layout.simple_list_item_2, android.R.id.text1, appList) {
                    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                        val view = super.getView(position, convertView, parent)
                        val item = getItem(position)
                        val text1 = view.findViewById<TextView>(android.R.id.text1)
                        val text2 = view.findViewById<TextView>(android.R.id.text2)

                        if (item != null) {
                            val cfg = item.config
                            if (cfg.enabled) {
                                val badges = mutableListOf<String>()
                                if (cfg.renderer.isNotEmpty()) badges.add("GPU: ${cfg.renderer}")
                                if (cfg.fakeCpuEnabled) badges.add("CPU: ${cfg.fakeCpuCores}C")
                                if (cfg.targetGlMajor > 0) badges.add("GLES ${cfg.targetGlMajor}.${cfg.targetGlMinor}")
                                if (cfg.shaderPreset != "None") badges.add("Shader: ${cfg.shaderPreset}")
                                if (cfg.frameGenMode != "Off") badges.add("FG: ${cfg.frameGenMode}")
                                text1.text = "${item.name} [ACTIVE]"
                                text1.setTextColor(Color.parseColor("#2E7D32"))
                                text2.text = "${item.packageName}\n${badges.joinToString(" | ")}"
                            } else {
                                text1.text = item.name
                                text1.setTextColor(Color.BLACK)
                                text2.text = item.packageName
                            }
                        }
                        return view
                    }
                }

                appListView.adapter = adapter
                appListView.setOnItemClickListener { _, _, pos, _ ->
                    val item = appList[pos]
                    val intent = Intent(this@MainActivity, AppDetailActivity::class.java).apply {
                        putExtra("EXTRA_PACKAGE_NAME", item.packageName)
                    }
                    startActivity(intent)
                }
            }
        }.start()
    }

    data class GlesInfo(val vendor: String, val renderer: String, val version: String, val extensions: String)

    private fun queryLiveGlesInfo(): GlesInfo {
        return try {
            val dpy = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            val vers = IntArray(2)
            EGL14.eglInitialize(dpy, vers, 0, vers, 1)
            val configAttr = intArrayOf(
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                EGL14.EGL_NONE
            )
            val configs = arrayOfNulls<EGLConfig>(1)
            val numConfig = IntArray(1)
            EGL14.eglChooseConfig(dpy, configAttr, 0, configs, 0, 1, numConfig, 0)
            val surfAttr = intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE)
            val surf = EGL14.eglCreatePbufferSurface(dpy, configs[0], surfAttr, 0)
            val ctxAttr = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE)
            val ctx = EGL14.eglCreateContext(dpy, configs[0], EGL14.EGL_NO_CONTEXT, ctxAttr, 0)
            EGL14.eglMakeCurrent(dpy, surf, surf, ctx)

            val vendor = GLES20.glGetString(GLES20.GL_VENDOR) ?: "Unknown"
            val renderer = GLES20.glGetString(GLES20.GL_RENDERER) ?: "Unknown"
            val version = GLES20.glGetString(GLES20.GL_VERSION) ?: "Unknown"
            val extensions = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""

            EGL14.eglMakeCurrent(dpy, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            EGL14.eglDestroySurface(dpy, surf)
            EGL14.eglDestroyContext(dpy, ctx)
            EGL14.eglTerminate(dpy)

            GlesInfo(vendor, renderer, version, extensions)
        } catch (e: Exception) {
            GlesInfo("Error", e.message ?: "Unknown", "Unknown", "")
        }
    }

    private fun getCpuModel(): String {
        try {
            val lines = File("/proc/cpuinfo").readLines()
            for (line in lines) {
                if (line.startsWith("Hardware") || line.startsWith("model name")) {
                    val parts = line.split(":", limit = 2)
                    if (parts.size == 2 && parts[1].trim().isNotEmpty()) {
                        return parts[1].trim()
                    }
                }
            }
        } catch (_: Exception) {}
        return Build.HARDWARE
    }
}
