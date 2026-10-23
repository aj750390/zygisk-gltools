package com.gltools.zygisk.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.PixelFormat
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.view.Choreographer
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import androidx.core.app.NotificationCompat
import com.gltools.zygisk.R

class FpsOverlayService : Service(), Choreographer.FrameCallback {

    private lateinit var windowManager: WindowManager
    private lateinit var overlayView: View
    private lateinit var tvMetrics: TextView

    private var frameCount = 0
    private var lastFpsTimestamp = 0L
    private var currentFps = 60

    private val handler = Handler(Looper.getMainLooper())
    private val telemetryRunnable = object : Runnable {
        override fun run() {
            updateTelemetryDisplay()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate() {
        super.onCreate()
        startForegroundNotification()
        setupOverlayWindow()
        Choreographer.getInstance().postFrameCallback(this)
        handler.post(telemetryRunnable)
    }

    private fun startForegroundNotification() {
        val channelId = "gltools_overlay_channel"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                channelId,
                "GLTools HUD Service",
                NotificationManager.IMPORTANCE_LOW
            )
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }

        val notification: Notification = NotificationCompat.Builder(this, channelId)
            .setContentTitle("GLTools Performance HUD")
            .setContentText("Active overlay measuring FPS & thermals")
            .setSmallIcon(R.drawable.ic_speed)
            .build()

        startForeground(101, notification)
    }

    private fun setupOverlayWindow() {
        windowManager = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        overlayView = LayoutInflater.from(this).inflate(R.layout.layout_fps_overlay, null)
        tvMetrics = overlayView.findViewById(R.id.tvMetrics)

        val layoutType = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        } else {
            WindowManager.LayoutParams.TYPE_PHONE
        }

        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            layoutType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 100
            y = 150
        }

        // Draggable touch listener
        overlayView.setOnTouchListener(object : View.OnTouchListener {
            private var initialX = 0
            private var initialY = 0
            private var initialTouchX = 0f
            private var initialTouchY = 0f

            override fun onTouch(v: View, event: MotionEvent): Boolean {
                when (event.action) {
                    MotionEvent.ACTION_DOWN -> {
                        initialX = params.x
                        initialY = params.y
                        initialTouchX = event.rawX
                        initialTouchY = event.rawY
                        return true
                    }
                    MotionEvent.ACTION_MOVE -> {
                        params.x = initialX + (event.rawX - initialTouchX).toInt()
                        params.y = initialY + (event.rawY - initialTouchY).toInt()
                        windowManager.updateViewLayout(overlayView, params)
                        return true
                    }
                }
                return false
            }
        })

        windowManager.addView(overlayView, params)
    }

    override fun doFrame(frameTimeNanos: Long) {
        frameCount++
        val now = System.currentTimeMillis()
        if (lastFpsTimestamp == 0L) {
            lastFpsTimestamp = now
        } else if (now - lastFpsTimestamp >= 1000) {
            currentFps = frameCount
            frameCount = 0
            lastFpsTimestamp = now
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    private fun updateTelemetryDisplay() {
        val batteryIntent = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val tempTenths = batteryIntent?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) ?: 0
        val tempCelsius = tempTenths / 10.0

        tvMetrics.text = String.format("%d FPS | %.1f°C", currentFps, tempCelsius)
    }

    override fun onDestroy() {
        super.onDestroy()
        Choreographer.getInstance().removeFrameCallback(this)
        handler.removeCallbacks(telemetryRunnable)
        if (::overlayView.isInitialized) {
            windowManager.removeView(overlayView)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null
}
