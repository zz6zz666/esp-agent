/*
 * desktop_main_android.c — Full agent entry point for Android
 *
 * Mirrors main_desktop.c flow: data dir setup → config read →
 * display create → app_claw_start → cap registration →
 * emote engine → main loop.
 *
 * Omitted vs desktop: CLI args, daemon, signal handlers, tray icon,
 * SDL2 window management, lua_module_input (uses stub).
 */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "app_claw.h"
#include "app_capabilities.h"
#include "cap_cli.h"
#include "cap_lua_sandbox.h"
#include "cJSON.h"
#include "claw_core.h"
#include "claw_event_publisher.h"
#include "claw_event_router.h"
#include "claw_memory.h"
#include "claw_skill.h"
#include "display_arbiter.h"
#include "display_hal.h"
#include "display_hal_android.h"
#include "emote.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "desktop_android";

extern volatile bool s_agent_should_stop;

/* Stubs / forward declarations from sim_hal */
extern bool display_hal_is_active(void);
extern void display_hal_set_always_hide(bool hide);
extern bool display_hal_is_lua_mode(void);
extern void display_hal_main_loop_wait(uint32_t timeout_ms);
extern bool display_hal_recreate_emote(void);

extern void emote_set_network_msg(const char *msg);
extern void emote_stop(void);
extern const char *g_screenshots_dir;

extern esp_err_t cap_screenshot_register_group(void);
extern const char *g_emote_config_path;
extern esp_err_t cap_emote_text_register_group(void);

/* Stub for SDL2-based input module (Android has no SDL2) */
extern int lua_module_input_register(void);

static int mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

static void write_default_json(const char *path, const char *content)
{
    if (access(path, F_OK) == 0) return;
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

static void ensure_default_router_rules(const char *path)
{
    if (access(path, F_OK) == 0) return;

    const char *json =
        "[\n"
        "  {\n"
        "    \"id\": \"im_any_message_working_reply\",\n"
        "    \"description\": \"Reply to any IM text message with a working acknowledgement.\",\n"
        "    \"enabled\": true,\n"
        "    \"consume_on_match\": false,\n"
        "    \"ack\": \"{{event.source_channel}} working reply sent\",\n"
        "    \"match\": {\n"
        "      \"event_type\": \"message\",\n"
        "      \"event_key\": \"text\",\n"
        "      \"content_type\": \"text\"\n"
        "    },\n"
        "    \"actions\": [\n"
        "      {\n"
        "        \"type\": \"send_message\",\n"
        "        \"input\": {\n"
        "          \"channel\": \"{{event.source_channel}}\",\n"
        "          \"chat_id\": \"{{event.chat_id}}\",\n"
        "          \"message\": \"ESP-Claw is working on it...\"\n"
        "        }\n"
        "      }\n"
        "    ]\n"
        "  },\n"
        "  {\n"
        "    \"id\": \"im_any_message_agent\",\n"
        "    \"description\": \"Route IM text messages to the agent.\",\n"
        "    \"enabled\": true,\n"
        "    \"consume_on_match\": true,\n"
        "    \"ack\": \"{{event.source_channel}} routed to agent\",\n"
        "    \"match\": {\n"
        "      \"event_type\": \"message\",\n"
        "      \"event_key\": \"text\",\n"
        "      \"content_type\": \"text\"\n"
        "    },\n"
        "    \"actions\": [\n"
        "      {\n"
        "        \"type\": \"run_agent\",\n"
        "        \"input\": {\n"
        "          \"target_channel\": \"{{event.source_channel}}\",\n"
        "          \"session_policy\": \"chat\"\n"
        "        }\n"
        "      }\n"
        "    ]\n"
        "  },\n"
        "  {\n"
        "    \"id\": \"agent_stage_im_notify\",\n"
        "    \"description\": \"Deliver agent stage progress messages to the original IM chat.\",\n"
        "    \"enabled\": true,\n"
        "    \"consume_on_match\": true,\n"
        "    \"ack\": \"{{event.source_channel}} agent stage sent\",\n"
        "    \"match\": {\n"
        "      \"source_cap\": \"claw_core\",\n"
        "      \"event_type\": \"agent_stage\",\n"
        "      \"content_type\": \"text\"\n"
        "    },\n"
        "    \"actions\": [\n"
        "      {\n"
        "        \"type\": \"send_message\",\n"
        "        \"input\": {\n"
        "          \"channel\": \"{{event.source_channel}}\",\n"
        "          \"chat_id\": \"{{event.chat_id}}\",\n"
        "          \"message\": \"{{event.text}}\"\n"
        "        }\n"
        "      }\n"
        "    ]\n"
        "  },\n"
        "  {\n"
        "    \"id\": \"agent_out_message_send_message\",\n"
        "    \"description\": \"Deliver agent output messages to the original IM chat.\",\n"
        "    \"enabled\": true,\n"
        "    \"consume_on_match\": true,\n"
        "    \"ack\": \"{{event.source_channel}} agent output sent\",\n"
        "    \"match\": {\n"
        "      \"source_cap\": \"claw_core\",\n"
        "      \"event_type\": \"out_message\",\n"
        "      \"content_type\": \"text\"\n"
        "    },\n"
        "    \"actions\": [\n"
        "      {\n"
        "        \"type\": \"send_message\",\n"
        "        \"input\": {\n"
        "          \"channel\": \"{{event.source_channel}}\",\n"
        "          \"chat_id\": \"{{event.chat_id}}\",\n"
        "          \"message\": \"{{event.text}}\"\n"
        "        }\n"
        "      }\n"
        "    ]\n"
        "  }\n"
        "]\n";
    write_default_json(path, json);
}

#if defined(PLATFORM_ANDROID)
static const char *get_defaults_dir(char *buf, size_t bufsz)
{
    (void)buf; (void)bufsz;
    return "/nonexistent/crush-claw/defaults";
}
#else
static const char *get_defaults_dir(char *buf, size_t bufsz)
{
    (void)bufsz;
    return "/usr/share/crush-claw/defaults";
}
#endif

static void copy_tree(const char *src, const char *dst)
{
    mkdir_p(dst);

    DIR *d = opendir(src);
    if (!d) return;

    struct dirent *ent;
    char src_path[PATH_MAX];
    char dst_path[PATH_MAX];

    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        snprintf(src_path, sizeof(src_path), "%s/%s", src, ent->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, ent->d_name);

        struct stat st;
        if (stat(src_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            copy_tree(src_path, dst_path);
        } else {
            if (access(dst_path, F_OK) == 0) continue;
            FILE *fs = fopen(src_path, "rb");
            if (fs) {
                FILE *fd = fopen(dst_path, "wb");
                if (fd) {
                    char buf[8192];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), fs)) > 0)
                        fwrite(buf, 1, n, fd);
                    fclose(fd);
                }
                fclose(fs);
            }
        }
    }
    closedir(d);
}

static void seed_defaults(const char *data_dir)
{
    char defaults_dir[PATH_MAX];
    get_defaults_dir(defaults_dir, sizeof(defaults_dir));
    ESP_LOGI(TAG, "Seed defaults: source=%s", defaults_dir);
    if (access(defaults_dir, F_OK) != 0) {
        ESP_LOGW(TAG, "Seed defaults: source dir not found, skipping");
        return;
    }

    char seeded_marker[PATH_MAX];
    snprintf(seeded_marker, sizeof(seeded_marker), "%s/skills/cap_im_feishu/SKILL.md", data_dir);
    if (access(seeded_marker, F_OK) == 0) {
        ESP_LOGI(TAG, "Seed defaults: already seeded (%s exists)", seeded_marker);
        return;
    }

    ESP_LOGI(TAG, "Seeding defaults from %s", defaults_dir);
    copy_tree(defaults_dir, data_dir);
}

static void json_get_string(cJSON *obj, const char *key, char *out, size_t out_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(out, item->valuestring, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static void json_get_bool(cJSON *obj, const char *key, bool *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item))
        *out = cJSON_IsTrue(item);
}

static void json_get_int(cJSON *obj, const char *key, int *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item))
        *out = item->valueint;
}

int desktop_main_android(const char *data_dir)
{
    ESP_LOGI(TAG, "=== Crush Claw Android starting ===");
    ESP_LOGI(TAG, "Data dir: %s", data_dir);

    char abs_data_dir[PATH_MAX];
    strncpy(abs_data_dir, data_dir, sizeof(abs_data_dir) - 1);
    abs_data_dir[sizeof(abs_data_dir) - 1] = '\0';

    /* Build sub-paths */
    char abs_sessions[PATH_MAX];
    char abs_memory[PATH_MAX];
    char abs_skills[PATH_MAX];
    char abs_scripts[PATH_MAX];
    char abs_router_rules[PATH_MAX];
    char abs_scheduler[PATH_MAX];
    char abs_inbox[PATH_MAX];
    char abs_screenshots[PATH_MAX];
    snprintf(abs_sessions, sizeof(abs_sessions), "%s/sessions", abs_data_dir);
    snprintf(abs_memory, sizeof(abs_memory), "%s/memory", abs_data_dir);
    snprintf(abs_skills, sizeof(abs_skills), "%s/skills", abs_data_dir);
    snprintf(abs_scripts, sizeof(abs_scripts), "%s/scripts", abs_data_dir);
    snprintf(abs_router_rules, sizeof(abs_router_rules), "%s/router_rules/router_rules.json", abs_data_dir);
    snprintf(abs_scheduler, sizeof(abs_scheduler), "%s/scheduler/schedules.json", abs_data_dir);
    snprintf(abs_inbox, sizeof(abs_inbox), "%s/inbox", abs_data_dir);
    snprintf(abs_screenshots, sizeof(abs_screenshots), "%s/screenshots", abs_data_dir);

    mkdir_p(abs_sessions);
    mkdir_p(abs_memory);
    mkdir_p(abs_skills);
    mkdir_p(abs_scripts);
    {
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%s/builtin", abs_scripts);
        mkdir_p(tmp);
        snprintf(tmp, sizeof(tmp), "%s/router_rules", abs_data_dir);
        mkdir_p(tmp);
        snprintf(tmp, sizeof(tmp), "%s/scheduler", abs_data_dir);
        mkdir_p(tmp);
    }
    mkdir_p(abs_inbox);
    mkdir_p(abs_screenshots);
    g_screenshots_dir = abs_screenshots;

    /* Set CRUSH_CLAW_DATA_DIR env for console_unix.c socket path */
    setenv("CRUSH_CLAW_DATA_DIR", abs_data_dir, 1);

    /* Set up log file (matches main_desktop.c) */
    {
        char log_path[PATH_MAX];
        snprintf(log_path, sizeof(log_path), "%s/agent.log", abs_data_dir);
        esp_log_set_log_file(log_path);
    }

    /* Seed from system defaults on first run (same as main_desktop.c) */
    seed_defaults(abs_data_dir);

    /* Default data files (same as main_desktop.c) */
    ensure_default_router_rules(abs_router_rules);
    write_default_json(abs_scheduler, "[]");

    /* Read config.json */
    static char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config.json", abs_data_dir);

    app_claw_config_t config;
    memset(&config, 0, sizeof(config));
    bool display_enabled = true;
    int  lcd_width  = 480;
    int  lcd_height = 480;
    char emote_text[64] = "";

    /* Write default config.json if missing (matches main_desktop.c template) */
    {
        char full_config_path[PATH_MAX];
        snprintf(full_config_path, sizeof(full_config_path), "%s/config.json", abs_data_dir);
        if (access(full_config_path, F_OK) != 0) {
            const char *default_config =
                "{\n"
                "  \"llm\": {\n"
                "    \"api_key\": \"\",\n"
                "    \"model\": \"deepseek-v4-flash\",\n"
                "    \"profile\": \"anthropic\",\n"
                "    \"base_url\": \"https://api.deepseek.com/anthropic\",\n"
                "    \"auth_type\": \"\",\n"
                "    \"timeout_ms\": \"120000\",\n"
                "    \"max_tokens\": \"8192\"\n"
                "  },\n"
                "  \"channels\": {\n"
                "    \"local_im\": { \"enabled\": true },\n"
                "    \"qq\":      { \"enabled\": false, \"app_id\": \"\", \"app_secret\": \"\" },\n"
                "    \"telegram\": { \"enabled\": false, \"bot_token\": \"\" },\n"
                "    \"feishu\":  { \"enabled\": false, \"app_id\": \"\", \"app_secret\": \"\" },\n"
                "    \"wechat\":  { \"enabled\": false, \"token\": \"\",\n"
                "                   \"base_url\": \"https://ilinkai.weixin.qq.com\",\n"
                "                   \"cdn_base_url\": \"https://novac2c.cdn.weixin.qq.com/c2c\",\n"
                "                   \"account_id\": \"default\" }\n"
                "  },\n"
                "  \"search\": {\n"
                "    \"brave_key\": \"\",\n"
                "    \"tavily_key\": \"\"\n"
                "  },\n"
                "  \"display\": {\n"
                "    \"enabled\": true,\n"
                "    \"lcd_width\": 480,\n"
                "    \"lcd_height\": 480,\n"
                "    \"emote_text\": \"\",\n"
                "    \"emu_scale\": \"1.5\",\n"
                "    \"lua_scale\": \"1.5\"\n"
                "  },\n"
                "  \"session\": {\n"
                "    \"context_token_budget\": \"96256\",\n"
                "    \"max_message_chars\": \"8192\",\n"
                "    \"compress_threshold_percent\": \"80\"\n"
                "  }\n"
                "}\n";
            FILE *fp = fopen(full_config_path, "wb");
            if (fp) { fputs(default_config, fp); fclose(fp); }
        }
    }

    /* Parse config.json if exists */
    {
        char full_config_path[PATH_MAX];
        snprintf(full_config_path, sizeof(full_config_path), "%s/config.json", abs_data_dir);
        FILE *fp = fopen(full_config_path, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            rewind(fp);
            char *buf = calloc(1, (size_t)sz + 1);
            if (buf && fread(buf, 1, (size_t)sz, fp) > 0) {
                buf[sz] = '\0';
                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    cJSON *llm = cJSON_GetObjectItemCaseSensitive(root, "llm");
                    if (llm) {
                        json_get_string(llm, "api_key", config.llm_api_key, sizeof(config.llm_api_key));
                        json_get_string(llm, "model", config.llm_model, sizeof(config.llm_model));
                        json_get_string(llm, "profile", config.llm_profile, sizeof(config.llm_profile));
                        json_get_string(llm, "base_url", config.llm_base_url, sizeof(config.llm_base_url));
                        json_get_string(llm, "auth_type", config.llm_auth_type, sizeof(config.llm_auth_type));
                        json_get_string(llm, "timeout_ms", config.llm_timeout_ms, sizeof(config.llm_timeout_ms));
                        json_get_string(llm, "max_tokens", config.llm_max_tokens, sizeof(config.llm_max_tokens));
                    }

                    cJSON *channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
                    if (channels) {
                        cJSON *qq = cJSON_GetObjectItemCaseSensitive(channels, "qq");
                        if (qq) {
                            json_get_string(qq, "app_id", config.qq_app_id, sizeof(config.qq_app_id));
                            json_get_string(qq, "app_secret", config.qq_app_secret, sizeof(config.qq_app_secret));
                        }
                        cJSON *tg = cJSON_GetObjectItemCaseSensitive(channels, "telegram");
                        if (tg) {
                            json_get_string(tg, "bot_token", config.tg_bot_token, sizeof(config.tg_bot_token));
                        }
                        cJSON *feishu = cJSON_GetObjectItemCaseSensitive(channels, "feishu");
                        if (feishu) {
                            json_get_string(feishu, "app_id", config.feishu_app_id, sizeof(config.feishu_app_id));
                            json_get_string(feishu, "app_secret", config.feishu_app_secret, sizeof(config.feishu_app_secret));
                        }
                        cJSON *wechat = cJSON_GetObjectItemCaseSensitive(channels, "wechat");
                        if (wechat) {
                            json_get_string(wechat, "token", config.wechat_token, sizeof(config.wechat_token));
                            json_get_string(wechat, "base_url", config.wechat_base_url, sizeof(config.wechat_base_url));
                            json_get_string(wechat, "cdn_base_url", config.wechat_cdn_base_url, sizeof(config.wechat_cdn_base_url));
                            json_get_string(wechat, "account_id", config.wechat_account_id, sizeof(config.wechat_account_id));
                        }
                    }

                    cJSON *search = cJSON_GetObjectItemCaseSensitive(root, "search");
                    if (search) {
                        json_get_string(search, "brave_key", config.search_brave_key, sizeof(config.search_brave_key));
                        json_get_string(search, "tavily_key", config.search_tavily_key, sizeof(config.search_tavily_key));
                    }

                    cJSON *display = cJSON_GetObjectItemCaseSensitive(root, "display");
                    if (display) {
                        json_get_bool(display, "enabled", &display_enabled);
                        json_get_int(display, "lcd_width", &lcd_width);
                        json_get_int(display, "lcd_height", &lcd_height);
                        json_get_string(display, "emote_text", emote_text, sizeof(emote_text));
                        if (lcd_width < 320)  lcd_width  = 320;
                        if (lcd_height < 240) lcd_height = 240;
                    }

                    cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session");
                    if (session) {
                        json_get_string(session, "context_token_budget", config.session_context_token_budget, sizeof(config.session_context_token_budget));
                        json_get_string(session, "max_message_chars", config.session_max_message_chars, sizeof(config.session_max_message_chars));
                        json_get_string(session, "compress_threshold_percent", config.session_compress_threshold_percent, sizeof(config.session_compress_threshold_percent));
                    }

                    cJSON_Delete(root);
                }
                free(buf);
            }
            fclose(fp);
        }
    }

    /* Defaults */
    if (!config.llm_profile[0])
        strncpy(config.llm_profile, "openai", sizeof(config.llm_profile) - 1);
    if (!config.llm_timeout_ms[0])
        strncpy(config.llm_timeout_ms, "30000", sizeof(config.llm_timeout_ms) - 1);
    if (!config.llm_max_tokens[0])
        strncpy(config.llm_max_tokens, "4096", sizeof(config.llm_max_tokens) - 1);
    if (!config.session_context_token_budget[0])
        strncpy(config.session_context_token_budget, "96256", sizeof(config.session_context_token_budget) - 1);
    if (!config.session_max_message_chars[0])
        strncpy(config.session_max_message_chars, "4096", sizeof(config.session_max_message_chars) - 1);
    if (!config.session_compress_threshold_percent[0])
        strncpy(config.session_compress_threshold_percent, "80", sizeof(config.session_compress_threshold_percent) - 1);

    ESP_LOGI(TAG, "Display: %s", display_enabled ? "enabled" : "disabled");
    ESP_LOGI(TAG, "LLM config: profile=%s model=%s",
             config.llm_profile,
             config.llm_model[0] ? config.llm_model : "(unset)");

    /* Create display (floating overlay window) */
    if (display_enabled) {
        /* Ensure clean state for re-entry: previous run may not have
         * completed display_hal_destroy, leaving stale pointers that
         * would cause create() to take the worker/Lua path. */
        extern display_android_ctx_t g_display_ctx;
        if (g_display_ctx.pixels) {
            free(g_display_ctx.pixels);
            g_display_ctx.pixels = NULL;
        }
        if (g_display_ctx.pixels_draw) {
            free(g_display_ctx.pixels_draw);
            g_display_ctx.pixels_draw = NULL;
        }
        g_display_ctx.lua_mode = false;

        esp_err_t d_err = display_hal_create(NULL, NULL, 0, lcd_width, lcd_height);
        if (d_err != ESP_OK) {
            ESP_LOGW(TAG, "display_hal_create failed: %d", (int)d_err);
        }
    }

    /* Init TrueType font system once (persists across Lua display cycles) */
    {
        extern esp_err_t font_android_init(const char *data_dir);
        g_font_android_ok = (font_android_init(abs_data_dir) == ESP_OK);
    }

    /* Storage paths */
    app_claw_storage_paths_t paths;
    memset(&paths, 0, sizeof(paths));
    strncpy(paths.fatfs_base_path, abs_data_dir, sizeof(paths.fatfs_base_path) - 1);
    strncpy(paths.memory_session_root, abs_sessions, sizeof(paths.memory_session_root) - 1);
    strncpy(paths.memory_root_dir, abs_memory, sizeof(paths.memory_root_dir) - 1);
    strncpy(paths.skills_root_dir, abs_skills, sizeof(paths.skills_root_dir) - 1);
    strncpy(paths.lua_root_dir, abs_scripts, sizeof(paths.lua_root_dir) - 1);
    strncpy(paths.router_rules_path, abs_router_rules, sizeof(paths.router_rules_path) - 1);
    strncpy(paths.scheduler_rules_path, abs_scheduler, sizeof(paths.scheduler_rules_path) - 1);
    strncpy(paths.im_attachment_root, abs_inbox, sizeof(paths.im_attachment_root) - 1);

    /* Prune skills_list.json: hide IM skills for channels not configured */
    {
        char sk_list[PATH_MAX];
        snprintf(sk_list, sizeof(sk_list), "%s/skills/skills_list.json", abs_data_dir);
        FILE *sf = fopen(sk_list, "rb");
        if (sf) {
            fseek(sf, 0, SEEK_END);
            long ssz = ftell(sf);
            rewind(sf);
            char *sbuf = calloc(1, (size_t)ssz + 1);
            if (sbuf && fread(sbuf, 1, (size_t)ssz, sf) > 0) {
                sbuf[ssz] = '\0';
                cJSON *skroot = cJSON_Parse(sbuf);
                if (skroot) {
                    cJSON *skills_arr = cJSON_GetObjectItemCaseSensitive(skroot, "skills");
                    if (cJSON_IsArray(skills_arr)) {
                        int remove_flags = 0;
                        #define SKILL_QQ      1
                        #define SKILL_TG      2
                        #define SKILL_FEISHU  4
                        #define SKILL_WECHAT  8
                        if (!config.qq_app_id[0] || !config.qq_app_secret[0]) remove_flags |= SKILL_QQ;
                        if (!config.tg_bot_token[0])                    remove_flags |= SKILL_TG;
                        if (!config.feishu_app_id[0] || !config.feishu_app_secret[0]) remove_flags |= SKILL_FEISHU;
                        if (!config.wechat_token[0])                    remove_flags |= SKILL_WECHAT;
                        if (remove_flags) {
                            int len = cJSON_GetArraySize(skills_arr);
                            for (int i = len - 1; i >= 0; i--) {
                                cJSON *entry = cJSON_GetArrayItem(skills_arr, i);
                                const char *fid = cJSON_GetObjectItemCaseSensitive(entry, "id") ? cJSON_GetObjectItemCaseSensitive(entry, "id")->valuestring : NULL;
                                if (fid) {
                                    int drop = 0;
                                    if ((remove_flags & SKILL_QQ)     && !strcmp(fid, "cap_im_qq"))     drop = 1;
                                    if ((remove_flags & SKILL_TG)     && !strcmp(fid, "cap_im_tg"))     drop = 1;
                                    if ((remove_flags & SKILL_FEISHU) && !strcmp(fid, "cap_im_feishu")) drop = 1;
                                    if ((remove_flags & SKILL_WECHAT) && !strcmp(fid, "cap_im_wechat")) drop = 1;
                                    if (drop) cJSON_DeleteItemFromArray(skills_arr, i);
                                }
                            }
                        }
                        char *filtered = cJSON_Print(skroot);
                        if (filtered) {
                            cJSON_Delete(skroot);
                            fclose(sf);
                            FILE *wf = fopen(sk_list, "wb");
                            if (wf) { fputs(filtered, wf); fclose(wf); }
                            free(filtered);
                            goto prune_done;
                        }
                    }
                    cJSON_Delete(skroot);
                }
                free(sbuf);
            }
            prune_done:
            fclose(sf);
        }
    }

    /* Register modules before agent start */
    lua_module_input_register();
    cap_lua_sandbox_init(abs_data_dir);

    /* Start agent core (registers all capabilities, event router, memory, skills) */
    ESP_LOGI(TAG, "Starting app_claw...");
    esp_err_t err = app_claw_start(&config, &paths);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app_claw_start failed: %d", (int)err);
        /* ESP_ERR_INVALID_STATE (259) means core init (event router etc.)
         * was already done in a previous run.  The components are still
         * alive — treat as non-fatal and continue. */
        if (err != 0x103) {  /* ESP_ERR_INVALID_STATE = 0x103 */
            display_hal_destroy();
            return 1;
        }
    }
    ESP_LOGI(TAG, "app_claw_start OK");

    /* Register cap_cli (allow LLM to run safe CLI commands) */
    {
        cap_cli_config_t cli_cfg = {
            .max_commands = 16,
            .max_output_bytes = 4096,
        };
        if (cap_cli_init(&cli_cfg) == ESP_OK) {
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "help", .description = "list commands",
                .usage_hint = "help" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "lua", .description = "manage Lua scripts",
                .usage_hint = "lua --list|--run|--write" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "skill", .description = "manage skills",
                .usage_hint = "skill --list|--activate|--deactivate" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "event_router", .description = "manage event routing",
                .usage_hint = "event_router --rules|--add-rule-json" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "session", .description = "manage sessions",
                .usage_hint = "session [id]" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "cap", .description = "manage capabilities",
                .usage_hint = "cap list|groups" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "auto", .description = "auto rules",
                .usage_hint = "auto rules|reload" });
            cap_cli_register_command(&(cap_cli_command_t){
                .command_name = "display", .description = "display control",
                .usage_hint = "display on|off|status" });
            cap_cli_register_group();
            ESP_LOGI(TAG, "cap_cli registered with 8 allowed commands");
        }
    }

    /* Register desktop-only capabilities */
    cap_screenshot_register_group();
    ESP_LOGI(TAG, "cap_screenshot registered");

    g_emote_config_path = config_path;
    cap_emote_text_register_group();
    ESP_LOGI(TAG, "cap_emote_text registered");

    /* Set custom emote text before starting emote engine */
    if (emote_text[0]) {
        emote_set_network_msg(emote_text);
    }

    /* Start emote engine (boot animation) */
    err = app_claw_ui_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Emote engine failed (%d), using fallback boot text", (int)err);
        if (display_enabled && display_hal_is_active()) {
            display_hal_begin_frame(true, 0x18E3);
            int w = display_hal_width();
            display_hal_draw_text_aligned(0, 16, 320, 24,
                "Crush Claw", 2,
                0xFFDF, false, 0,
                DISPLAY_HAL_TEXT_ALIGN_CENTER, DISPLAY_HAL_TEXT_VALIGN_TOP);
            display_hal_draw_line(40, 48, 280, 48, 0x52AA);
            display_hal_draw_text(16, 60, "Android Port", 1, 0xCE59, false, 0);
            {
                char info[64];
                snprintf(info, sizeof(info), "Display: %dx%d", w, display_hal_height());
                display_hal_draw_text(16, 76, info, 1, 0xCE59, false, 0);
            }
            display_hal_draw_text(16, 92, "Emote engine unavailable", 1, 0xCE59, false, 0);
            display_hal_present();
            display_hal_end_frame();
        }
    }

    /* Re-acquire emote display ownership in case the arbiter was
     * released during a previous stop (we keep the emote alive
     * across stop/start to avoid gfx_emote_deinit hangs). */
    display_arbiter_acquire(DISPLAY_ARBITER_OWNER_EMOTE);

    ESP_LOGI(TAG, "Agent running. Entering main loop.");
    while (!s_agent_should_stop) {

        if (display_hal_is_active()) {
            if (display_hal_is_lua_mode()) {
                display_hal_main_loop_wait(100);
            } else {
                display_hal_present();
                display_hal_main_loop_wait(16);
            }
        } else {
            display_hal_main_loop_wait(100);
        }
    }

    ESP_LOGI(TAG, "Shutting down...");
    /* Stop all running Lua async jobs (cooperative hook cancel, 2s grace).
     * Must come before display-related teardown because Lua scripts may
     * hold the display arbiter and render frames. */
    {
        extern esp_err_t cap_lua_stop_all_jobs(const char *exclusive_filter,
                                              uint32_t wait_ms,
                                              char *output,
                                              size_t output_size);
        esp_err_t lua_err = cap_lua_stop_all_jobs(NULL, 2000, NULL, 0);
        if (lua_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop Lua jobs: %d", (int)lua_err);
        }
    }
    /* Release display-arbiter so emote flush callback stops rendering
     * into our pixel buffer before we free it.  DO NOT call emote_stop()
     * — its gfx_emote_deinit() blocks waiting for a GFX task ack and
     * can hang on Android's FreeRTOS shim.  The emote subsystem stays
     * alive across stop/start cycles. */
    display_arbiter_release(DISPLAY_ARBITER_OWNER_EMOTE);
    display_hal_destroy();
    ESP_LOGI(TAG, "Shutdown complete");
    return 0;
}
