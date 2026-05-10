package com.crushclaw

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.DisplayMetrics
import android.util.Log
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import androidx.core.app.NotificationCompat
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Foreground service that manages:
 * 1. The native agent process (C code via JNI)
 * 2. Floating window overlay(s) for emote + Lua display
 * 3. Show/hide/switch logic matching Windows desktop behavior
 *
 * Architecture matches sim_hal/display_sdl2.c:
 * - Single floating window, destroyed & recreated on mode switch
 * - Emote mode:  320x240 (+ optional title bar), always-on-top
 * - Lua mode:    configurable (default 480x480), no title bar
 * - Hide/show:   removeView/addView to WindowManager
 * - Pixel-perfect: fixed pixel dimensions, no system scaling
 */
class FloatingWindowService : Service() {

    companion object {
        private const val TAG = "FloatingWindowService"
        private const val NOTIFICATION_ID = 1
        private const val CHANNEL_ID = "crushclaw_agent"
        private const val CHANNEL_NAME = "Crush Claw Agent"

        // Broadcast actions for notification buttons
        private const val ACTION_TOGGLE_VISIBLE = "com.crushclaw.TOGGLE_VISIBLE"
        private const val ACTION_TOGGLE_ALWAYS_HIDE = "com.crushclaw.TOGGLE_ALWAYS_HIDE"

        // Fixed dimensions (matching desktop)
        private const val EMOTE_W = 320
        private const val EMOTE_H = 240
        private const val TITLE_BAR_H = 20

        // Default Lua display size
        private const val LUA_DEFAULT_W = 480
        private const val LUA_DEFAULT_H = 480

        // Scale factors (default 1.5x, overridable in config.json display.emu_scale / display.lua_scale)
        @Volatile var emuScale = 1.5f
        @Volatile var luaScale = 1.5f

        @Volatile
        var isRunning = false
            private set

        fun start(context: Context) {
            isRunning = true
            val intent = Intent(context, FloatingWindowService::class.java)
            ContextCompat.startServiceSafely(context, intent)
        }

        fun stop(context: Context) {
            isRunning = false
            val intent = Intent(context, FloatingWindowService::class.java)
            context.stopService(intent)
        }
    }

    private lateinit var windowManager: WindowManager
    private var overlayView: View? = null
    private var imageView: ImageView? = null
    private var titleBarView: View? = null

    private var renderBitmap: Bitmap? = null
    private var nativeThreadStarted = AtomicBoolean(false)

    // Window state (mirrors display_sdl2.c sdl_ctx_t)
    @Volatile private var isLuaMode = false
    @Volatile private var emoteVisible = true
    @Volatile private var emoteWasVisible = true
    @Volatile private var alwaysHide = false
    @Volatile private var currentWidth = EMOTE_W
    @Volatile private var currentHeight = EMOTE_H
    @Volatile private var currentScale = 1.5f
    @Volatile private var luaWidth = LUA_DEFAULT_W
    @Volatile private var luaHeight = LUA_DEFAULT_H
    @Volatile private var pendingSwitch = false
    @Volatile private var pendingSwitchTarget = false // false=emote, true=lua

    // Position tracking (persisted to SharedPreferences)
    @Volatile private var windowX = 0
    @Volatile private var windowY = 100

    private val mainHandler = Handler(Looper.getMainLooper())
    private var nativeBridge: NativeBridge = NativeBridge()

    override fun onCreate() {
        super.onCreate()
        windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        createNotificationChannel()
        loadScaleFromConfig()
        loadWindowPosition()

        // Reset visibility state on fresh start
        emoteVisible = true
        emoteWasVisible = true
        alwaysHide = false

        val filter = IntentFilter().apply {
            addAction(ACTION_TOGGLE_VISIBLE)
            addAction(ACTION_TOGGLE_ALWAYS_HIDE)
        }
        registerReceiver(notificationReceiver, filter, 0x4) // RECEIVER_NOT_EXPORTED

        val notification = buildNotification()
        startForeground(NOTIFICATION_ID, notification)
        ensureNativeThreadStarted()
    }

    private fun loadScaleFromConfig() {
        try {
            val configFile = CrushClawApp.instance.configFile
            if (configFile.exists()) {
                val json = org.json.JSONObject(configFile.readText())
                val display = json.optJSONObject("display")
                if (display != null) {
                    val es = display.optDouble("emu_scale", 1.5)
                    val ls = display.optDouble("lua_scale", 1.5)
                    emuScale = es.toFloat().coerceIn(0.5f, 4.0f)
                    luaScale = ls.toFloat().coerceIn(0.5f, 4.0f)
                }
            }
        } catch (_: Exception) {}
    }

    private fun loadWindowPosition() {
        val prefs = getSharedPreferences("crushclaw_window", MODE_PRIVATE)
        windowX = prefs.getInt("window_x", 0)
        windowY = prefs.getInt("window_y", 100)
    }

    private fun saveWindowPosition() {
        val prefs = getSharedPreferences("crushclaw_window", MODE_PRIVATE)
        prefs.edit().putInt("window_x", windowX).putInt("window_y", windowY).apply()
    }

    private fun ensureNativeThreadStarted() {
        if (nativeThreadStarted.compareAndSet(false, true)) {
            Thread({
                val dataPath = CrushClawApp.instance.clawDataDirPath()
                extractAssetFonts(dataPath)
                nativeBridge.nativeStart(dataPath, this@FloatingWindowService)
            }, "claw-native").start()
        }
    }

    /**
     * Copy bundled TrueType fonts from assets/fonts/ to {data_dir}/fonts/
     * so the native font_android module can discover and load them.
     * Only copies if the destination doesn't already exist.
     */
    private fun extractAssetFonts(dataPath: String) {
        try {
            val fontDir = java.io.File(dataPath, "fonts")
            fontDir.mkdirs()
            val assetFonts = assets.list("fonts") ?: return
            for (name in assetFonts) {
                val dst = java.io.File(fontDir, name)
                if (!dst.exists()) {
                    assets.open("fonts/$name").use { input ->
                        java.io.FileOutputStream(dst).use { output ->
                            input.copyTo(output)
                        }
                    }
                    Log.i(TAG, "Extracted asset font: $name")
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Font asset extraction failed: ${e.message}")
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        isRunning = true
        ensureNativeThreadStarted()
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        isRunning = false
        unregisterReceiver(notificationReceiver)
        val bridge = nativeBridge
        Thread({
            bridge.nativeStop()
            nativeThreadStarted.set(false)
        }, "claw-stop").start()
        destroyWindow()
        super.onDestroy()
    }

    // ================================================================
    // Window management (called from native/JNI or main thread)
    // ================================================================

    /**
     * Called from native when display_hal_create initializes the display.
     * Creates the first floating window (emote mode).
     * @param luaW  Configured Lua display width (for later mode switch)
     * @param luaH  Configured Lua display height
     */
    fun onDisplayCreate(luaW: Int, luaH: Int) {
        Log.i(TAG, "onDisplayCreate: luaW=$luaW luaH=$luaH")
        luaWidth = luaW
        luaHeight = luaH
        emoteVisible = true
        isLuaMode = false
        if (!alwaysHide) {
            mainHandler.post { rebuildWindow(EMOTE_W, EMOTE_H) }
        }
    }

    /**
     * Called from native when display ownership changes (emote ↔ Lua).
     * @param ownerMode  0=none, 1=lua, 2=emote
     * @param width      New window width
     * @param height     New window height
     */
    fun onDisplayOwnerChanged(ownerMode: Int, width: Int, height: Int) {
        Log.i(TAG, "onDisplayOwnerChanged: owner=$ownerMode w=$width h=$height")
        if (ownerMode == 1) {
            /* Emote → Lua: always show Lua unless Always Hide is on */
            emoteWasVisible = emoteVisible
            isLuaMode = true
            if (width > 0) { luaWidth = width }
            if (height > 0) { luaHeight = height }
            if (!alwaysHide) {
                mainHandler.post { rebuildWindow(luaWidth, luaHeight) }
            }
        } else if (ownerMode == 2) {
            /* Lua → Emote: restore emote visibility state */
            isLuaMode = false
            if (emoteWasVisible && !alwaysHide) {
                emoteVisible = true
                mainHandler.post { rebuildWindow(EMOTE_W, EMOTE_H) }
            } else {
                emoteVisible = false
                mainHandler.post { hideWindow() }
            }
        } else {
            /* ownerMode == 0: full shutdown */
            isLuaMode = false
            emoteVisible = false
            mainHandler.post { destroyWindow() }
        }
    }

    /**
     * Destroy + recreate the floating window at the given size.
     * Everything happens in a single handler message — no interleaving.
     */
    private fun rebuildWindow(w: Int, h: Int) {
        if (overlayView != null) {
            try { windowManager.removeView(overlayView) } catch (_: Exception) {}
            overlayView = null
            imageView = null
            titleBarView = null
        }

        currentWidth = w
        currentHeight = h
        val isLua = (w != EMOTE_W)
        val scale = if (isLua) luaScale else emuScale
        currentScale = scale
        val scaledTitleBarH = (TITLE_BAR_H * scale).toInt().coerceIn(16, 80)
        val viewW = (w * scale).toInt()
        val viewH = (h * scale).toInt()

        if (renderBitmap == null || renderBitmap!!.width != w || renderBitmap!!.height != h) {
            renderBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565)
            renderBitmap!!.eraseColor(Color.BLACK)
        }

        /* Shadow wrapper — holds elevation + rounded corners.
           Placed OUTSIDE the content container so it doesn't recalculate
           every frame when the ImageView is invalidated. */
        val density = resources.displayMetrics.density
        val cornerRad = 4f * density
        val shadowWrapper = FrameLayout(this).apply {
            outlineProvider = object : android.view.ViewOutlineProvider() {
                override fun getOutline(view: View, outline: android.graphics.Outline) {
                    outline.setRoundRect(0, 0, view.width, view.height, cornerRad)
                }
            }
            clipToOutline = true
            elevation = 8f * density
            setBackgroundColor(Color.TRANSPARENT)
        }

        val container = FrameLayout(this).apply {
            setBackgroundColor(if (isLua) Color.BLACK else Color.TRANSPARENT)
        }

        imageView = ImageView(this).apply {
            setImageBitmap(renderBitmap)
            scaleType = ImageView.ScaleType.FIT_XY
            layoutParams = FrameLayout.LayoutParams(viewW, viewH).apply {
                topMargin = scaledTitleBarH
            }
        }
        container.addView(imageView)

        titleBarView = buildTitleBar(viewW, scaledTitleBarH, scale) {
            if (isLuaMode) {
                /* Lua mode: hide Lua window, preserve emoteVisible */
                hideWindow()
            } else {
                /* Emote mode: same as notification Hide */
                emoteVisible = false
                hideWindow()
            }
            rebuildNotification()
        }
        container.addView(titleBarView)

        shadowWrapper.addView(container)

        val params = WindowManager.LayoutParams(
            viewW, viewH + scaledTitleBarH,
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            else
                WindowManager.LayoutParams.TYPE_PHONE,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                or WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                or WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = windowX
            y = windowY
        }

        shadowWrapper.setOnTouchListener(createTouchListener(params))
        windowManager.addView(shadowWrapper, params)
        overlayView = shadowWrapper

        Log.i(TAG, "rebuildWindow: ${viewW}x${viewH + scaledTitleBarH} lua=$isLua scale=$scale")
        notifyFrameReady()
    }

    /**
     * Called from native when display is destroyed.
     */
    fun onDisplayDestroy() {
        Log.i(TAG, "onDisplayDestroy")
        emoteVisible = false
        isLuaMode = false
        mainHandler.post { destroyWindow() }
    }

    /**
     * Called from native to enable/disable the overlay window.
     */
    fun onDisplayEnable(enable: Boolean) {
        if (enable && !isLuaMode) {
            if (!emoteVisible && !alwaysHide && overlayView == null) {
                emoteVisible = true
                mainHandler.post { rebuildWindow(EMOTE_W, EMOTE_H) }
            }
        } else if (!enable && !isLuaMode) {
            if (emoteVisible && overlayView != null) {
                emoteVisible = false
                mainHandler.post { hideWindow() }
            }
        }
    }

    /**
     * Called from native when a new frame is ready for display.
     * Copy the native RGB565 buffer into our Bitmap and invalidate.
     */
    fun onFrameReady(pixels: ByteArray, width: Int, height: Int) {
        if (width == 0 || height == 0) return

        val expectedW = if (isLuaMode) luaWidth else EMOTE_W
        val expectedH = if (isLuaMode) luaHeight else EMOTE_H
        if (width != expectedW || height != expectedH) {
            return
        }

        // Recreate bitmap if dimensions changed
        if (renderBitmap == null || renderBitmap!!.width != width || renderBitmap!!.height != height) {
            renderBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
        }

        val bmp = renderBitmap ?: return
        try {
            val buf = java.nio.ByteBuffer.allocateDirect(pixels.size)
            buf.put(pixels)
            buf.rewind()
            bmp.copyPixelsFromBuffer(buf)
        } catch (e: Exception) {
            Log.w(TAG, "Frame render failed: ${e.message}")
        }

        mainHandler.post {
            if (bmp.width == currentWidth && bmp.height == currentHeight) {
                imageView?.setImageBitmap(bmp)
                imageView?.invalidate()
            }
        }
    }

    /**
     * Notify the native side that we're ready to receive pixel data.
     */
    private fun notifyFrameReady() {
        nativeBridge.nativeNotifyFrameReady()
    }

    // ================================================================
    // Floating window create/destroy (matches SDL2 behavior)
    // ================================================================

    /**
     * Build a pixel-perfect title bar matching desktop display_sdl2.c:
     *   TITLE_BAR_H = 20, TITLE_BAR_BTN_W = 20
     *   bg = #282828, separator = #555555, btn = #4a4a4a, arrow = #bbbbbb
     *   Arrow is a 3-line downward triangle (pixel-perfect Canvas draw, not a font glyph).
     */
    private fun buildTitleBar(width: Int, barHeight: Int, scale: Float, onMinimize: () -> Unit): View {
        val bar = FrameLayout(this)
        bar.setBackgroundColor(Color.argb(255, 0x28, 0x28, 0x28))

        // Bottom edge separator (1px line, color #555555)
        val sep = View(this)
        sep.setBackgroundColor(Color.argb(255, 0x55, 0x55, 0x55))
        val sepLp = FrameLayout.LayoutParams(width, 1)
        sepLp.gravity = Gravity.BOTTOM
        bar.addView(sep, sepLp)

        // Minimize button: centered minus symbol
        val scaledBtnW = barHeight  // desktop: BTN_W == TITLE_BAR_H
        val scaledBtnH = barHeight - (4 * scale).toInt().coerceIn(8, barHeight)
        val scaledBtnMargin = (2 * scale).toInt().coerceIn(1, 10)

        val btn = FrameLayout(this)
        btn.setBackgroundColor(Color.argb(255, 0x4a, 0x4a, 0x4a))  // #4a4a4a
        btn.setOnClickListener { onMinimize() }

        // Minus line: small horizontal bar centered in the button
        val line = View(this)
        line.setBackgroundColor(Color.argb(255, 0xbb, 0xbb, 0xbb))  // #bbbbbb
        val lineH = (2 * scale).toInt().coerceIn(1, 6)
        val lineW = (scaledBtnW * 0.4f).toInt().coerceIn(4, scaledBtnW - 4)
        val lineLp = FrameLayout.LayoutParams(lineW, lineH)
        lineLp.gravity = Gravity.CENTER
        btn.addView(line, lineLp)

        val btnLp = FrameLayout.LayoutParams(scaledBtnW, scaledBtnH)
        btnLp.gravity = Gravity.TOP or Gravity.END
        btnLp.topMargin = scaledBtnMargin
        btnLp.rightMargin = scaledBtnMargin
        bar.addView(btn, btnLp)

        bar.layoutParams = FrameLayout.LayoutParams(width, barHeight)
        return bar
    }

    private fun hideWindow() {
        overlayView?.let {
            windowManager.removeView(it)
        }
        overlayView = null
        imageView = null
        titleBarView = null
        Log.i(TAG, "Window hidden")
    }

    private fun destroyWindow() {
        hideWindow()
        currentWidth = EMOTE_W
        currentHeight = EMOTE_H
        emoteVisible = false
    }

    // ================================================================
    // Touch handling (drag to reposition, tap to interact)
    // ================================================================

    private fun createTouchListener(params: WindowManager.LayoutParams): View.OnTouchListener {
        var initialX = 0
        var initialY = 0
        var initialTouchX = 0f
        var initialTouchY = 0f
        var dragging = false
        val touchSlop = 10

        // Determine display density for input coordinate translation
        val dm = resources.displayMetrics

        return View.OnTouchListener { v, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    initialX = params.x
                    initialY = params.y
                    initialTouchX = event.rawX
                    initialTouchY = event.rawY
                    dragging = false

                    // Forward touch to native (emote animation reaction)
                    val logicalX = (event.x / currentScale).toInt()
                    val logicalY = if (isLuaMode)
                        (event.y / currentScale).toInt()
                    else
                        ((event.y - TITLE_BAR_H * currentScale) / currentScale).toInt().coerceAtLeast(0)
                    nativeBridge.nativeInjectTouch(0, logicalX, logicalY)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - initialTouchX
                    val dy = event.rawY - initialTouchY
                    if (kotlin.math.abs(dx) > touchSlop || kotlin.math.abs(dy) > touchSlop) {
                        dragging = true
                        params.x = initialX + dx.toInt()
                        params.y = initialY + dy.toInt()
                        windowManager.updateViewLayout(v, params)
                        windowX = params.x
                        windowY = params.y
                    }
                    true
                }
                MotionEvent.ACTION_UP -> {
                    if (!dragging) {
                        // Single tap — notify native
                        nativeBridge.nativeInjectTouch(1, 0, 0)
                    } else {
                        nativeBridge.nativeInjectTouch(-1, 0, 0)
                        saveWindowPosition()
                    }
                    true
                }
                else -> false
            }
        }
    }

    // ================================================================
    // Notification
    // ================================================================

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, CHANNEL_NAME,
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Crush Claw agent service"
                setShowBadge(false)
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        val openIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val toggleVisibleIntent = PendingIntent.getBroadcast(
            this, 1,
            Intent(ACTION_TOGGLE_VISIBLE).setPackage(packageName),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val alwaysHideLabel = if (alwaysHide) "Always Hide: ON" else "Always Hide: OFF"
        val toggleAlwaysHideIntent = PendingIntent.getBroadcast(
            this, 2,
            Intent(ACTION_TOGGLE_ALWAYS_HIDE).setPackage(packageName),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        val visLabel = if (overlayView != null) "Hide Window" else "Show Window"
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Crush Claw")
            .setContentText(if (alwaysHide) "Window hidden (Always)" else "Agent running")
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentIntent(openIntent)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .addAction(0, visLabel, toggleVisibleIntent)
            .addAction(0, alwaysHideLabel, toggleAlwaysHideIntent)
            .build()
    }

    private fun rebuildNotification() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIFICATION_ID, buildNotification())
    }

    private val notificationReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            when (intent?.action) {
                ACTION_TOGGLE_VISIBLE -> {
                    if (alwaysHide) return
                    if (isLuaMode) {
                        /* Lua mode: toggle Lua window without changing emoteVisible */
                        if (overlayView != null) {
                            hideWindow()
                        } else {
                            rebuildWindow(luaWidth, luaHeight)
                        }
                    } else {
                        /* Emote mode: toggle emote window */
                        emoteVisible = !emoteVisible
                        if (emoteVisible && overlayView == null) {
                            rebuildWindow(EMOTE_W, EMOTE_H)
                        } else if (!emoteVisible && overlayView != null) {
                            hideWindow()
                        }
                    }
                    rebuildNotification()
                }
                ACTION_TOGGLE_ALWAYS_HIDE -> {
                    alwaysHide = !alwaysHide
                    if (alwaysHide) {
                        destroyWindow()
                    } else {
                        if (isLuaMode) {
                            rebuildWindow(luaWidth, luaHeight)
                        } else {
                            emoteVisible = true
                            if (overlayView == null) {
                                rebuildWindow(EMOTE_W, EMOTE_H)
                            }
                        }
                    }
                    rebuildNotification()
                }
            }
        }
    }
}

/** Extension because [ContextCompat] doesn't always exist. */
private object ContextCompat {
    fun startServiceSafely(context: Context, intent: Intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent)
        } else {
            context.startService(intent)
        }
    }
}
