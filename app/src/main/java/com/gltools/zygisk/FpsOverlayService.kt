package com.gltools.zygisk

import android.app.Service
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Build
import android.os.IBinder
import android.provider.Settings
import android.view.Gravity
import android.view.Choreographer
import android.view.WindowManager
import android.widget.TextView

class FpsOverlayService : Service(), Choreographer.FrameCallback {

    private var windowManager: WindowManager? = null
    private var fpsTextView: TextView? = null
    private var lastFrameTimeNanos: Long = 0
    private var frameCount: Int = 0
    private var isRunning: Boolean = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            stopSelf()
            return
        }
        windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        fpsTextView = TextView(this).apply {
            text = "FPS: --"
            setTextColor(Color.GREEN)
            textSize = 14f
            setBackgroundColor(Color.argb(140, 0, 0, 0))
            setPadding(16, 8, 16, 8)
        }

        val layoutFlag = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        } else {
            @Suppress("DEPRECATION")
            WindowManager.LayoutParams.TYPE_PHONE
        }

        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            layoutFlag,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 24
            y = 48
        }

        try {
            windowManager?.addView(fpsTextView, params)
            isRunning = true
            Choreographer.getInstance().postFrameCallback(this)
        } catch (_: Exception) {}
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!isRunning) return
        if (lastFrameTimeNanos == 0L) {
            lastFrameTimeNanos = frameTimeNanos
        }
        frameCount++
        val diff = frameTimeNanos - lastFrameTimeNanos
        if (diff >= 1_000_000_000L) {
            val fps = (frameCount * 1_000_000_000L) / diff
            fpsTextView?.text = "FPS: $fps"
            frameCount = 0
            lastFrameTimeNanos = frameTimeNanos
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onDestroy() {
        isRunning = false
        try {
            if (fpsTextView != null) {
                windowManager?.removeView(fpsTextView)
            }
        } catch (_: Exception) {}
        super.onDestroy()
    }
}
