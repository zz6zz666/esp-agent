package com.crushclaw

import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import org.json.JSONObject
import java.io.File

/**
 * Configuration editor — reads & writes config.json.
 * Provides the minimal UI requested: LLM key, model, display toggle, search keys.
 */
class ConfigActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "ConfigActivity"
    }

    private lateinit var apiKeyEdit: EditText
    private lateinit var modelEdit: EditText
    private lateinit var profileEdit: EditText
    private lateinit var baseUrlEdit: EditText
    private lateinit var authTypeEdit: EditText
    private lateinit var timeoutEdit: EditText
    private lateinit var maxTokensEdit: EditText
    private lateinit var braveKeyEdit: EditText
    private lateinit var tavilyKeyEdit: EditText
    private lateinit var emoteTextEdit: EditText
    private lateinit var emuScaleEdit: EditText
    private lateinit var luaScaleEdit: EditText
    private lateinit var contextBudgetEdit: EditText
    private lateinit var maxMsgCharsEdit: EditText
    private lateinit var compressPctEdit: EditText
    private lateinit var saveBtn: Button
    private lateinit var config: JSONObject
    private lateinit var headerView: View

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

        // Blue header bar
        headerView = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.parseColor("#1565C0"))
            setPadding(16, 12, 16, 12)
            addView(TextView(context).apply {
                text = "Configuration"
                textSize = 18f
                setTextColor(Color.WHITE)
            })
        }
        root.addView(headerView)

        // Content area
        val contentLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 24, 48, 48)
        }

        fun addLabel(text: String): TextView {
            return TextView(this).apply {
                this.text = text
                textSize = 13f
                setPadding(0, 14, 0, 2)
            }
        }

        fun addEdit(defaultValue: String): EditText {
            return EditText(this).apply {
                setText(defaultValue)
                textSize = 14f
                setSingleLine(true)
            }
        }

        fun addSection(title: String) {
            contentLayout.addView(TextView(this).apply {
                text = title
                textSize = 16f
                setPadding(0, 20, 0, 6)
                setTypeface(null, android.graphics.Typeface.BOLD)
            })
        }

        // ── LLM section ──
        addSection("LLM")
        val llm = config.optJSONObject("llm") ?: JSONObject()
        contentLayout.addView(addLabel("API Key"))
        apiKeyEdit = addEdit(llm.optString("api_key", ""))
        contentLayout.addView(apiKeyEdit)

        contentLayout.addView(addLabel("Model"))
        modelEdit = addEdit(llm.optString("model", "deepseek-v4-flash"))
        contentLayout.addView(modelEdit)

        contentLayout.addView(addLabel("Profile (openai, anthropic, deepseek, ...)"))
        profileEdit = addEdit(llm.optString("profile", "anthropic"))
        contentLayout.addView(profileEdit)

        contentLayout.addView(addLabel("Base URL"))
        baseUrlEdit = addEdit(llm.optString("base_url", "https://api.deepseek.com/anthropic"))
        contentLayout.addView(baseUrlEdit)

        contentLayout.addView(addLabel("Auth Type (leave empty for auto)"))
        authTypeEdit = addEdit(llm.optString("auth_type", ""))
        contentLayout.addView(authTypeEdit)

        contentLayout.addView(addLabel("Timeout (ms)"))
        timeoutEdit = addEdit(llm.optString("timeout_ms", "120000"))
        contentLayout.addView(timeoutEdit)

        contentLayout.addView(addLabel("Max Tokens"))
        maxTokensEdit = addEdit(llm.optString("max_tokens", "8192"))
        contentLayout.addView(maxTokensEdit)

        // ── Display section ──
        addSection("Display")
        val display = config.optJSONObject("display") ?: JSONObject()
        contentLayout.addView(addLabel("Emote Text (toast overlay on LCD)"))
        emoteTextEdit = addEdit(display.optString("emote_text", ""))
        contentLayout.addView(emoteTextEdit)

        contentLayout.addView(addLabel("Emote Window Scale (0.5-4.0, default 1.5)"))
        emuScaleEdit = addEdit(display.optString("emu_scale", "1.5"))
        contentLayout.addView(emuScaleEdit)

        contentLayout.addView(addLabel("Lua Window Scale (0.5-4.0, default 1.5)"))
        luaScaleEdit = addEdit(display.optString("lua_scale", "1.5"))
        contentLayout.addView(luaScaleEdit)

        // ── Search section ──
        addSection("Web Search")
        val search = config.optJSONObject("search") ?: JSONObject()
        contentLayout.addView(addLabel("Brave Search API Key"))
        braveKeyEdit = addEdit(search.optString("brave_key", ""))
        contentLayout.addView(braveKeyEdit)

        contentLayout.addView(addLabel("Tavily Search API Key"))
        tavilyKeyEdit = addEdit(search.optString("tavily_key", ""))
        contentLayout.addView(tavilyKeyEdit)

        // ── Session section ──
        addSection("Session")
        val session = config.optJSONObject("session") ?: JSONObject()
        contentLayout.addView(addLabel("Context Token Budget"))
        contextBudgetEdit = addEdit(session.optString("context_token_budget", "96256"))
        contentLayout.addView(contextBudgetEdit)

        contentLayout.addView(addLabel("Max Message Chars"))
        maxMsgCharsEdit = addEdit(session.optString("max_message_chars", "8192"))
        contentLayout.addView(maxMsgCharsEdit)

        contentLayout.addView(addLabel("Compress Threshold (%)"))
        compressPctEdit = addEdit(session.optString("compress_threshold_percent", "80"))
        contentLayout.addView(compressPctEdit)

        // Save button
        saveBtn = Button(this).apply {
            text = "Save Configuration"
            setOnClickListener { saveConfig() }
        }
        contentLayout.addView(saveBtn)

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

    private fun loadConfig() {
        val configFile = CrushClawApp.instance.configFile
        if (configFile.exists()) {
            try {
                config = JSONObject(configFile.readText())
            } catch (e: Exception) {
                Log.w(TAG, "Failed to parse config.json: ${e.message}")
                config = createDefaultConfig()
            }
        } else {
            config = createDefaultConfig()
        }
    }

    private fun createDefaultConfig(): JSONObject {
        return JSONObject().apply {
            put("llm", JSONObject().apply {
                put("api_key", "")
                put("model", "deepseek-v4-flash")
                put("profile", "anthropic")
                put("base_url", "https://api.deepseek.com/anthropic")
                put("auth_type", "")
                put("timeout_ms", "120000")
                put("max_tokens", "8192")
            })
            put("channels", JSONObject().apply {
                put("local_im", JSONObject().apply { put("enabled", true) })
                put("qq", JSONObject().apply { put("enabled", false); put("app_id", ""); put("app_secret", "") })
                put("telegram", JSONObject().apply { put("enabled", false); put("bot_token", "") })
                put("feishu", JSONObject().apply { put("enabled", false); put("app_id", ""); put("app_secret", "") })
                put("wechat", JSONObject().apply {
                    put("enabled", false)
                    put("token", "")
                    put("base_url", "https://ilinkai.weixin.qq.com")
                    put("cdn_base_url", "https://novac2c.cdn.weixin.qq.com/c2c")
                    put("account_id", "default")
                })
            })
            put("search", JSONObject().apply {
                put("brave_key", "")
                put("tavily_key", "")
            })
            put("display", JSONObject().apply {
                put("enabled", true)
                put("lcd_width", 480)
                put("lcd_height", 480)
                put("emote_text", "")
                put("emu_scale", "1.5")
                put("lua_scale", "1.5")
            })
            put("session", JSONObject().apply {
                put("context_token_budget", "96256")
                put("max_message_chars", "8192")
                put("compress_threshold_percent", "80")
            })
        }
    }

    private fun saveConfig() {
        try {
            // Update LLM
            var llm = config.optJSONObject("llm")
            if (llm == null) { llm = JSONObject(); config.put("llm", llm) }
            llm.put("api_key", apiKeyEdit.text.toString())
            llm.put("model", modelEdit.text.toString())
            llm.put("profile", profileEdit.text.toString())
            llm.put("base_url", baseUrlEdit.text.toString())
            llm.put("auth_type", authTypeEdit.text.toString())
            llm.put("timeout_ms", timeoutEdit.text.toString())
            llm.put("max_tokens", maxTokensEdit.text.toString())

            // Update Search
            var search = config.optJSONObject("search")
            if (search == null) { search = JSONObject(); config.put("search", search) }
            search.put("brave_key", braveKeyEdit.text.toString())
            search.put("tavily_key", tavilyKeyEdit.text.toString())

            // Update Display
            var display = config.optJSONObject("display")
            if (display == null) { display = JSONObject(); config.put("display", display) }
            display.put("emote_text", emoteTextEdit.text.toString())
            display.put("emu_scale", emuScaleEdit.text.toString())
            display.put("lua_scale", luaScaleEdit.text.toString())

            // Update Session
            var session = config.optJSONObject("session")
            if (session == null) { session = JSONObject(); config.put("session", session) }
            session.put("context_token_budget", contextBudgetEdit.text.toString())
            session.put("max_message_chars", maxMsgCharsEdit.text.toString())
            session.put("compress_threshold_percent", compressPctEdit.text.toString())

            // Write to file
            val configFile = CrushClawApp.instance.configFile
            configFile.parentFile?.mkdirs()
            configFile.writeText(config.toString(2))

            Toast.makeText(this, "Configuration saved", Toast.LENGTH_SHORT).show()
            finish()
        } catch (e: Exception) {
            Toast.makeText(this, "Save failed: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }
}
