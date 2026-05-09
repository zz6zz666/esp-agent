package com.crushclaw

import android.graphics.Color
import android.os.Bundle
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import org.json.JSONObject
import java.io.File

class ChannelConfigActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "ChannelConfigActivity"
    }

    private lateinit var config: JSONObject
    private lateinit var headerView: View

    // QQ
    private lateinit var qqEnabled: CheckBox
    private lateinit var qqAppId: EditText
    private lateinit var qqSecret: EditText

    // Telegram
    private lateinit var tgEnabled: CheckBox
    private lateinit var tgToken: EditText

    // Feishu
    private lateinit var fsEnabled: CheckBox
    private lateinit var fsAppId: EditText
    private lateinit var fsSecret: EditText

    // WeChat
    private lateinit var wxEnabled: CheckBox
    private lateinit var wxToken: EditText
    private lateinit var wxBaseUrl: EditText
    private lateinit var wxCdnUrl: EditText
    private lateinit var wxAccountId: EditText

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        loadConfig()
        createLayout()
        applyInsets()
    }

    private fun createLayout() {
        val scroll = ScrollView(this)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }

        headerView = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.parseColor("#1565C0"))
            setPadding(16, 12, 16, 12)
            addView(TextView(context).apply {
                text = "Channel Configuration"
                textSize = 18f
                setTextColor(Color.WHITE)
            })
        }
        root.addView(headerView)

        val contentLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 24, 48, 48)
        }

        fun addLabel(text: String): TextView {
            return TextView(this).apply {
                this.text = text
                textSize = 13f
                setPadding(0, 10, 0, 2)
            }
        }

        fun addEdit(defaultValue: String): EditText {
            return EditText(this).apply {
                setText(defaultValue)
                textSize = 14f
                setSingleLine(true)
            }
        }

        fun addCheck(label: String, default: Boolean): CheckBox {
            return CheckBox(this).apply {
                text = label
                textSize = 14f
                isChecked = default
                setPadding(0, 12, 0, 4)
            }
        }

        fun addSection(title: String) {
            contentLayout.addView(TextView(this).apply {
                text = title
                textSize = 16f
                setPadding(0, 18, 0, 6)
                setTypeface(null, android.graphics.Typeface.BOLD)
            })
        }

        // ── QQ ──
        addSection("QQ Bot")
        val qq = config.optJSONObject("channels")?.optJSONObject("qq") ?: JSONObject()
        qqEnabled = addCheck("Enable QQ Channel", qq.optBoolean("enabled", false))
        contentLayout.addView(qqEnabled)
        contentLayout.addView(addLabel("App ID"))
        qqAppId = addEdit(qq.optString("app_id", ""))
        contentLayout.addView(qqAppId)
        contentLayout.addView(addLabel("App Secret"))
        qqSecret = addEdit(qq.optString("app_secret", ""))
        contentLayout.addView(qqSecret)

        // ── Telegram ──
        addSection("Telegram Bot")
        val tg = config.optJSONObject("channels")?.optJSONObject("telegram") ?: JSONObject()
        tgEnabled = addCheck("Enable Telegram Channel", tg.optBoolean("enabled", false))
        contentLayout.addView(tgEnabled)
        contentLayout.addView(addLabel("Bot Token"))
        tgToken = addEdit(tg.optString("bot_token", ""))
        contentLayout.addView(tgToken)

        // ── Feishu ──
        addSection("Feishu / Lark")
        val fs = config.optJSONObject("channels")?.optJSONObject("feishu") ?: JSONObject()
        fsEnabled = addCheck("Enable Feishu Channel", fs.optBoolean("enabled", false))
        contentLayout.addView(fsEnabled)
        contentLayout.addView(addLabel("App ID"))
        fsAppId = addEdit(fs.optString("app_id", ""))
        contentLayout.addView(fsAppId)
        contentLayout.addView(addLabel("App Secret"))
        fsSecret = addEdit(fs.optString("app_secret", ""))
        contentLayout.addView(fsSecret)

        // ── WeChat ──
        addSection("WeChat Official Account")
        val wx = config.optJSONObject("channels")?.optJSONObject("wechat") ?: JSONObject()
        wxEnabled = addCheck("Enable WeChat Channel", wx.optBoolean("enabled", false))
        contentLayout.addView(wxEnabled)
        contentLayout.addView(addLabel("Token"))
        wxToken = addEdit(wx.optString("token", ""))
        contentLayout.addView(wxToken)
        contentLayout.addView(addLabel("Base URL"))
        wxBaseUrl = addEdit(wx.optString("base_url", "https://ilinkai.weixin.qq.com"))
        contentLayout.addView(wxBaseUrl)
        contentLayout.addView(addLabel("CDN Base URL"))
        wxCdnUrl = addEdit(wx.optString("cdn_base_url", "https://novac2c.cdn.weixin.qq.com/c2c"))
        contentLayout.addView(wxCdnUrl)
        contentLayout.addView(addLabel("Account ID"))
        wxAccountId = addEdit(wx.optString("account_id", "default"))
        contentLayout.addView(wxAccountId)

        // Save button
        val saveBtn = Button(this).apply {
            text = "Save Channel Configuration"
            setOnClickListener { saveConfig() }
        }
        contentLayout.addView(saveBtn)

        root.addView(contentLayout)
        scroll.addView(root)
        setContentView(scroll)
    }

    private fun loadConfig() {
        val configFile = CrushClawApp.instance.configFile
        if (configFile.exists()) {
            try {
                config = JSONObject(configFile.readText())
                if (!config.has("channels")) config.put("channels", JSONObject())
            } catch (e: Exception) {
                config = JSONObject()
                config.put("channels", JSONObject())
            }
        } else {
            config = JSONObject()
            config.put("channels", JSONObject())
        }
    }

    private fun saveConfig() {
        try {
            var channels = config.optJSONObject("channels")
            if (channels == null) { channels = JSONObject(); config.put("channels", channels) }

            fun ensureChannel(name: String): JSONObject {
                var ch = channels.optJSONObject(name)
                if (ch == null) { ch = JSONObject(); channels.put(name, ch) }
                return ch
            }

            val qq = ensureChannel("qq")
            qq.put("enabled", qqEnabled.isChecked)
            qq.put("app_id", qqAppId.text.toString())
            qq.put("app_secret", qqSecret.text.toString())

            val tg = ensureChannel("telegram")
            tg.put("enabled", tgEnabled.isChecked)
            tg.put("bot_token", tgToken.text.toString())

            val fs = ensureChannel("feishu")
            fs.put("enabled", fsEnabled.isChecked)
            fs.put("app_id", fsAppId.text.toString())
            fs.put("app_secret", fsSecret.text.toString())

            val wx = ensureChannel("wechat")
            wx.put("enabled", wxEnabled.isChecked)
            wx.put("token", wxToken.text.toString())
            wx.put("base_url", wxBaseUrl.text.toString())
            wx.put("cdn_base_url", wxCdnUrl.text.toString())
            wx.put("account_id", wxAccountId.text.toString())

            val configFile = CrushClawApp.instance.configFile
            configFile.parentFile?.mkdirs()
            configFile.writeText(config.toString(2))

            Toast.makeText(this, "Channel configuration saved", Toast.LENGTH_SHORT).show()
            finish()
        } catch (e: Exception) {
            Toast.makeText(this, "Save failed: ${e.message}", Toast.LENGTH_LONG).show()
        }
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
}
