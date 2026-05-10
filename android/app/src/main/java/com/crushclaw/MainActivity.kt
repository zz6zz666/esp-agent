package com.crushclaw

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.DocumentsContract
import android.provider.Settings
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "MainActivity"
    }

    private lateinit var statusText: TextView
    private lateinit var startStopBtn: Button
    private lateinit var floatingPermBtn: Button
    private lateinit var notificationPermBtn: Button
    private lateinit var configBtn: Button
    private lateinit var channelBtn: Button

    private lateinit var headerView: View

    private val overlayPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        updateUi()
    }

    private val notificationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            maybeStartService()
        }
        updateUi()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        createLayout()
        applyInsets()
        maybeStartService()
    }

    override fun onResume() {
        super.onResume()
        updateUi()
    }

    private fun createLayout() {
        val scroll = ScrollView(this)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }

        // Blue header bar
        headerView = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.parseColor("#1565C0"))
            setPadding(16, 12, 16, 12)
            addView(TextView(context).apply {
                text = "Crush Claw"
                textSize = 18f
                setTextColor(Color.WHITE)
            })
        }
        root.addView(headerView)

        // Content area with padding
        val contentLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 24, 48, 48)
        }

        // Status
        statusText = TextView(this).apply {
            textSize = 14f
            setPadding(0, 0, 0, 24)
        }
        contentLayout.addView(statusText)

        // Config button
        configBtn = Button(this).apply {
            text = "Manage Configuration"
            setOnClickListener { openConfigEditor() }
        }
        contentLayout.addView(configBtn)

        // Channel config button
        channelBtn = Button(this).apply {
            text = "Channel Configuration"
            setOnClickListener { openChannelEditor() }
        }
        contentLayout.addView(channelBtn)

        // Browse files button
        val filesBtn = Button(this).apply {
            text = "Browse Files"
            setOnClickListener { openFileBrowser() }
        }
        contentLayout.addView(filesBtn)

        // Floating window permission
        floatingPermBtn = Button(this).apply {
            setOnClickListener { requestOverlayPermission() }
        }
        contentLayout.addView(floatingPermBtn)

        // Notification permission (Android 13+)
        notificationPermBtn = Button(this).apply {
            setOnClickListener { requestNotificationPermission() }
        }
        contentLayout.addView(notificationPermBtn)

        // Start/Stop service
        startStopBtn = Button(this).apply {
            setOnClickListener { toggleService() }
        }
        contentLayout.addView(startStopBtn)

        root.addView(contentLayout)
        scroll.addView(root)
        setContentView(scroll)
    }

    private fun applyInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(headerView) { view, insets ->
            val statusBars = insets.getInsets(WindowInsetsCompat.Type.statusBars())
            view.updatePadding(top = statusBars.top + 12)
            WindowInsetsCompat.CONSUMED
        }
        WindowCompat.setDecorFitsSystemWindows(window, false)
        window.statusBarColor = Color.parseColor("#0D47A1")
    }

    private fun updateUi() {
        val isServiceRunning = FloatingWindowService.isRunning
        val hasOverlayPerm = Settings.canDrawOverlays(this)
        val hasNotificationPerm = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) ==
                    PackageManager.PERMISSION_GRANTED
        } else true

        statusText.text = buildString {
            appendLine("Service: ${if (isServiceRunning) "RUNNING" else "STOPPED"}")
            appendLine("Overlay Permission: ${if (hasOverlayPerm) "GRANTED" else "NEEDED"}")
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                appendLine("Notification Permission: ${if (hasNotificationPerm) "GRANTED" else "NEEDED"}")
            }
        }

        floatingPermBtn.text = if (hasOverlayPerm) "Overlay: OK" else "Grant Overlay Permission"
        floatingPermBtn.isEnabled = !hasOverlayPerm

        notificationPermBtn.visibility = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && !hasNotificationPerm) {
            notificationPermBtn.text = "Grant Notification Permission"
            android.view.View.VISIBLE
        } else {
            android.view.View.GONE
        }

        startStopBtn.text = if (isServiceRunning) "Stop Agent" else "Start Agent"
        startStopBtn.isEnabled = hasOverlayPerm && hasNotificationPerm
    }

    private fun requestOverlayPermission() {
        if (!Settings.canDrawOverlays(this)) {
            val intent = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:$packageName")
            )
            overlayPermissionLauncher.launch(intent)
        }
    }

    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    private fun toggleService() {
        if (FloatingWindowService.isRunning) {
            FloatingWindowService.stop(this)
        } else {
            maybeStartService()
        }
        updateUi()
    }

    private fun maybeStartService() {
        if (FloatingWindowService.isRunning) return
        if (!Settings.canDrawOverlays(this)) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED
            ) return
        }
        FloatingWindowService.start(this)
        updateUi()
    }

    private fun openConfigEditor() {
        try {
            val intent = Intent(this, ConfigActivity::class.java)
            startActivity(intent)
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to open config: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun openChannelEditor() {
        try {
            val intent = Intent(this, ChannelConfigActivity::class.java)
            startActivity(intent)
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to open channel config: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun openFileBrowser() {
        try {
            val uri = Uri.parse("content://com.crushclaw.documents/root/claw_root")
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, DocumentsContract.Document.MIME_TYPE_DIR)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            startActivity(intent)
        } catch (e: Exception) {
            Toast.makeText(this, "Open Files app and look for \"Crush Claw\" in the sidebar", Toast.LENGTH_LONG).show()
        }
    }
}

// BootReceiver for auto-start on boot
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (Intent.ACTION_BOOT_COMPLETED == intent.action) {
            // Only auto-start if overlay permission is already granted
            if (Settings.canDrawOverlays(context)) {
                FloatingWindowService.start(context)
            }
        }
    }
}
