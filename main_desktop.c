/*
 * main_desktop.c — Desktop simulator entry point for esp-claw
 *
 * Replaces the ESP32 main.c.  Reads config from ~/.esp-agent/config.json,
 * stores data under ~/.esp-agent/, and optionally disables the SDL2 display.
 */
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app_claw.h"
#include "app_capabilities.h"
#include "cJSON.h"
#include "claw_core.h"
#include "claw_event_publisher.h"
#include "claw_event_router.h"
#include "claw_memory.h"
#include "claw_skill.h"
#include "display_hal.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ESP_AGENT_VERSION "1.0.0"
#define DEFAULTS_DIR       "/usr/share/esp-agent/defaults"

static const char *TAG = "desktop_main";
static volatile bool s_running = true;

/* daemon / display state */
static bool g_daemon_mode = false;
static char g_pid_file_path[PATH_MAX] = {0};

/* display_sdl2.c extensions (not in display_hal.h) */
extern bool display_hal_is_active(void);

/* ---- helpers ---- */

static void sig_handler(int sig)
{
    (void)sig;
    s_running = false;
    ESP_LOGI(TAG, "Shutdown signal received");
}

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

/*
 * Read an optional JSON string field.  If missing or not a string, *out is
 * left untouched (so callers can pre-fill defaults).
 */
static void json_get_string(cJSON *obj, const char *key, char *out, size_t out_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring)
        strncpy(out, item->valuestring, out_size - 1);
}

static void json_get_bool(cJSON *obj, const char *key, bool *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item))
        *out = cJSON_IsTrue(item);
}

/*
 * Create a default empty JSON file if it doesn't exist.
 */
static void write_default_json(const char *path, const char *content)
{
    if (access(path, F_OK) == 0) return;
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

/*
 * Recursively copy a directory tree from src/ to dst/.
 * Skips files that already exist in dst using cp -rn.
 */
static void copy_tree(const char *src, const char *dst)
{
    pid_t pid = fork();
    if (pid == 0) {
        execlp("cp", "cp", "-rn", "--", src, dst, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }
}

/*
 * If the data directory is empty and the system defaults directory exists,
 * seed it with default data files.
 */
static void seed_defaults(const char *data_dir)
{
    if (access(DEFAULTS_DIR, F_OK) != 0) return;

    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config.json", data_dir);
    if (access(config_path, F_OK) == 0) return;

    ESP_LOGI(TAG, "Seeding defaults from %s", DEFAULTS_DIR);
    copy_tree(DEFAULTS_DIR, data_dir);
}

/*
 * Write default router_rules.json if missing.
 */
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

/*
 * Write default skills_list.json if missing.
 */
static void ensure_default_skills(const char *skills_dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/skills_list.json", skills_dir);
    if (access(path, F_OK) == 0) return;

    const char *json =
        "{\n"
        "  \"skills\": [\n"
        "    {\n"
        "      \"id\": \"lua_runner\",\n"
        "      \"file\": \"lua_runner.md\",\n"
        "      \"summary\": \"Run Lua scripts for automation and hardware interaction.\",\n"
        "      \"cap_groups\": [\"cap_lua\"]\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"memory_ops\",\n"
        "      \"file\": \"memory_ops.md\",\n"
        "      \"summary\": \"Store, recall, update, search, and manage long-term memory.\",\n"
        "      \"cap_groups\": [\"claw_memory\"]\n"
        "    },\n"
        "    {\n"
        "      \"id\": \"file_ops\",\n"
        "      \"file\": \"file_ops.md\",\n"
        "      \"summary\": \"List, read, write, and delete files on the device.\",\n"
        "      \"cap_groups\": [\"cap_files\"]\n"
        "    }\n"
        "  ]\n"
        "}\n";
    write_default_json(path, json);

    const char *docs[3][2] = {
        {"lua_runner.md",
         "# Lua Script Runner\n\n"
         "Run Lua scripts for automation, data processing, and hardware interaction.\n\n"
         "## Capabilities\n"
         "- Execute Lua scripts with `run_lua` capability\n"
         "- Scripts can interact with device hardware and sensors\n"
         "- Async job support for long-running scripts\n\n"
         "## Usage\n"
         "Place Lua scripts in the scripts directory and call `run_lua` with the script name.\n"},
        {"memory_ops.md",
         "# Memory Operations\n\n"
         "Store, recall, update, search, and manage long-term memory for the agent.\n\n"
         "## Capabilities\n"
         "- `memory_store` — Save a new memory with label and content\n"
         "- `memory_recall` — Retrieve memories by label or keyword\n"
         "- `memory_update` — Modify an existing memory\n"
         "- `memory_forget` — Remove a memory by label\n"
         "- `memory_search` — Full-text search across all memories\n\n"
         "## Usage\n"
         "Activate this skill when the user asks to remember, recall, update, or forget information.\n"},
        {"file_ops.md",
         "# File Operations\n\n"
         "List, read, write, copy, move, and delete files on the device.\n\n"
         "## Capabilities\n"
         "- `read_file` — Read contents of a text file\n"
         "- `write_file` — Create or overwrite a text file\n"
         "- `delete_file` — Delete a file\n"
         "- `copy_file` — Copy a file to a new location\n"
         "- `move_file` — Move or rename a file\n"
         "- `list_dir` — Recursively list files, optionally filtered by prefix\n\n"
         "## Usage\n"
         "Activate this skill when you need to work with files on the device filesystem.\n"},
    };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s/%s", skills_dir, docs[i][0]);
        write_default_json(path, docs[i][1]);
    }
}

/* ---- daemonization ---- */

static void write_pid_file(void)
{
    if (!g_pid_file_path[0]) return;
    FILE *fp = fopen(g_pid_file_path, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
}

static void daemonize(const char *log_path)
{
    /* First fork — detach from terminal */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) _exit(0);  /* parent exits */

    /* New session */
    if (setsid() < 0) { perror("setsid"); exit(1); }

    /* Second fork — prevent re-acquiring a controlling TTY */
    pid = fork();
    if (pid < 0) { perror("fork2"); exit(1); }
    if (pid > 0) _exit(0);

    /* Write PID file now that we're the final daemon process */
    write_pid_file();

    /* Set umask, chdir to root */
    umask(022);
    chdir("/");

    /* Redirect all standard streams to /dev/null.
       Log output goes via esp_log_set_log_file which was called before daemonize. */
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        dup2(null_fd, STDOUT_FILENO);
        dup2(null_fd, STDERR_FILENO);
        close(null_fd);
    }

    /* Set log file for ESP_LOGX dual output */
    esp_log_set_log_file(log_path);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --data-dir <path>    Custom data directory (default: ~/.esp-agent)\n");
    printf("  --pid-file <path>    Write PID to file (used with --daemon)\n");
    printf("  --daemon             Run as background daemon\n");
    printf("  --foreground         Run in foreground (default, overrides --daemon)\n");
    printf("  --version            Print version and exit\n");
    printf("  --help               Show this help message\n");
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP,  sig_handler);

    /* ---- parse CLI arguments ---- */
    const char *data_dir_override = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("esp-agent %s\n", ESP_AGENT_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--daemon") == 0) {
            g_daemon_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--foreground") == 0) {
            g_daemon_mode = false;
            continue;
        }
        if (strcmp(argv[i], "--pid-file") == 0) {
            if (i + 1 < argc) {
                strncpy(g_pid_file_path, argv[++i], sizeof(g_pid_file_path) - 1);
            } else {
                fprintf(stderr, "--pid-file requires a path argument\n");
                return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "--data-dir") == 0) {
            if (i + 1 < argc) {
                data_dir_override = argv[++i];
            } else {
                fprintf(stderr, "--data-dir requires a path argument\n");
                return 1;
            }
            continue;
        }
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        print_usage(argv[0]);
        return 1;
    }

    /* ---- resolve data root ---- */
    char data_dir[PATH_MAX];

    if (data_dir_override) {
        snprintf(data_dir, sizeof(data_dir), "%s", data_dir_override);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(data_dir, sizeof(data_dir), "%s/.esp-agent", home);
    }

    /* Build sub-paths */
    char abs_data_dir[PATH_MAX];
    if (!realpath(data_dir, abs_data_dir)) {
        mkdir_p(data_dir);
        if (!realpath(data_dir, abs_data_dir)) {
            perror("realpath data_dir");
            return 1;
        }
    }

    /* Resolve log file path early (needed for daemon and esp_log_set_log_file) */
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof(log_path), "%s/agent.log", abs_data_dir);

    /* ---- daemonize if requested ---- */
    /* Set up log file for dual output (stderr + file) */
    esp_log_set_log_file(log_path);

    if (g_daemon_mode) {
        /* Default PID file if not explicitly set */
        if (!g_pid_file_path[0]) {
            snprintf(g_pid_file_path, sizeof(g_pid_file_path),
                     "%s/agent.pid", abs_data_dir);
        }
        daemonize(log_path);
    } else {
        if (g_pid_file_path[0]) write_pid_file();
    }

    printf("=== esp-claw Desktop Simulator v%s ===\n", ESP_AGENT_VERSION);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    char abs_sessions[PATH_MAX];
    char abs_memory[PATH_MAX];
    char abs_skills[PATH_MAX];
    char abs_scripts[PATH_MAX];
    char abs_router_rules[PATH_MAX];
    char abs_scheduler[PATH_MAX];
    char abs_inbox[PATH_MAX];
    snprintf(abs_sessions, sizeof(abs_sessions), "%s/sessions", abs_data_dir);
    snprintf(abs_memory, sizeof(abs_memory), "%s/memory", abs_data_dir);
    snprintf(abs_skills, sizeof(abs_skills), "%s/skills", abs_data_dir);
    snprintf(abs_scripts, sizeof(abs_scripts), "%s/scripts", abs_data_dir);
    snprintf(abs_router_rules, sizeof(abs_router_rules), "%s/router_rules/router_rules.json", abs_data_dir);
    snprintf(abs_scheduler, sizeof(abs_scheduler), "%s/scheduler/schedules.json", abs_data_dir);
    snprintf(abs_inbox, sizeof(abs_inbox), "%s/inbox", abs_data_dir);
#pragma GCC diagnostic pop

    /* Ensure all sub-directories exist */
    mkdir_p(abs_sessions);
    mkdir_p(abs_memory);
    mkdir_p(abs_skills);
    mkdir_p(abs_scripts);
    mkdir_p(abs_data_dir);
    {
        char tmp[PATH_MAX];
        snprintf(tmp, sizeof(tmp), "%s/builtin", abs_scripts);
        mkdir_p(tmp);
        snprintf(tmp, sizeof(tmp), "%s/router_rules", abs_data_dir);
        mkdir_p(tmp);
        snprintf(tmp, sizeof(tmp), "%s/scheduler", abs_data_dir);
        mkdir_p(tmp);
        mkdir_p(abs_inbox);
    }

    /* Seed from system defaults on first run */
    seed_defaults(abs_data_dir);

    /* Default data files (fallback if no system defaults) */
    ensure_default_skills(abs_skills);
    ensure_default_router_rules(abs_router_rules);
    write_default_json(abs_scheduler, "[]");

    /* ---- read config.json ---- */
    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "%s/config.json", abs_data_dir);

    app_claw_config_t config;
    memset(&config, 0, sizeof(config));
    bool display_enabled = true; /* default */

    /* Default config if missing */
    if (access(config_path, F_OK) != 0) {
        const char *default_config =
            "{\n"
            "  \"llm\": {\n"
            "    \"api_key\": \"\",\n"
            "    \"model\": \"\",\n"
            "    \"profile\": \"openai\",\n"
            "    \"base_url\": \"\",\n"
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
            "    \"enabled\": true\n"
            "  }\n"
            "}\n";
        write_default_json(config_path, default_config);
    }

    /* Parse config.json */
    {
        FILE *fp = fopen(config_path, "r");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            rewind(fp);
            char *buf = calloc(1, (size_t)sz + 1);
            if (buf && fread(buf, 1, (size_t)sz, fp) == (size_t)sz) {
                cJSON *root = cJSON_Parse(buf);
                if (root) {
                    /* LLM section */
                    cJSON *llm = cJSON_GetObjectItemCaseSensitive(root, "llm");
                    if (llm) {
                        json_get_string(llm, "api_key", config.llm_api_key,
                                        sizeof(config.llm_api_key));
                        json_get_string(llm, "model", config.llm_model,
                                        sizeof(config.llm_model));
                        json_get_string(llm, "profile", config.llm_profile,
                                        sizeof(config.llm_profile));
                        json_get_string(llm, "base_url", config.llm_base_url,
                                        sizeof(config.llm_base_url));
                        json_get_string(llm, "auth_type", config.llm_auth_type,
                                        sizeof(config.llm_auth_type));
                        json_get_string(llm, "timeout_ms", config.llm_timeout_ms,
                                        sizeof(config.llm_timeout_ms));
                        json_get_string(llm, "max_tokens", config.llm_max_tokens,
                                        sizeof(config.llm_max_tokens));
                    }

                    /* Channels section */
                    cJSON *channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
                    if (channels) {
                        cJSON *qq = cJSON_GetObjectItemCaseSensitive(channels, "qq");
                        if (qq) {
                            json_get_string(qq, "app_id", config.qq_app_id,
                                            sizeof(config.qq_app_id));
                            json_get_string(qq, "app_secret", config.qq_app_secret,
                                            sizeof(config.qq_app_secret));
                        }
                        cJSON *tg = cJSON_GetObjectItemCaseSensitive(channels, "telegram");
                        if (tg) {
                            json_get_string(tg, "bot_token", config.tg_bot_token,
                                            sizeof(config.tg_bot_token));
                        }
                        cJSON *feishu = cJSON_GetObjectItemCaseSensitive(channels, "feishu");
                        if (feishu) {
                            json_get_string(feishu, "app_id", config.feishu_app_id,
                                            sizeof(config.feishu_app_id));
                            json_get_string(feishu, "app_secret", config.feishu_app_secret,
                                            sizeof(config.feishu_app_secret));
                        }
                        cJSON *wechat = cJSON_GetObjectItemCaseSensitive(channels, "wechat");
                        if (wechat) {
                            json_get_string(wechat, "token", config.wechat_token,
                                            sizeof(config.wechat_token));
                            json_get_string(wechat, "base_url", config.wechat_base_url,
                                            sizeof(config.wechat_base_url));
                            json_get_string(wechat, "cdn_base_url", config.wechat_cdn_base_url,
                                            sizeof(config.wechat_cdn_base_url));
                            json_get_string(wechat, "account_id", config.wechat_account_id,
                                            sizeof(config.wechat_account_id));
                        }
                    }

                    /* Search section */
                    cJSON *search = cJSON_GetObjectItemCaseSensitive(root, "search");
                    if (search) {
                        json_get_string(search, "brave_key", config.search_brave_key,
                                        sizeof(config.search_brave_key));
                        json_get_string(search, "tavily_key", config.search_tavily_key,
                                        sizeof(config.search_tavily_key));
                    }

                    /* Display section */
                    cJSON *display = cJSON_GetObjectItemCaseSensitive(root, "display");
                    if (display)
                        json_get_bool(display, "enabled", &display_enabled);

                    cJSON_Delete(root);
                }
                free(buf);
            }
            fclose(fp);
        }
    }

    /* ---- environment overrides ---- */
    const char *env;
    if ((env = getenv("LLM_API_KEY")))
        strncpy(config.llm_api_key, env, sizeof(config.llm_api_key) - 1);
    if ((env = getenv("LLM_MODEL")))
        strncpy(config.llm_model, env, sizeof(config.llm_model) - 1);
    if ((env = getenv("LLM_BASE_URL")))
        strncpy(config.llm_base_url, env, sizeof(config.llm_base_url) - 1);
    if ((env = getenv("LLM_PROFILE")))
        strncpy(config.llm_profile, env, sizeof(config.llm_profile) - 1);
    if ((env = getenv("LLM_AUTH_TYPE")))
        strncpy(config.llm_auth_type, env, sizeof(config.llm_auth_type) - 1);

    /* Channel credential overrides */
    if ((env = getenv("QQ_APP_ID")))
        strncpy(config.qq_app_id, env, sizeof(config.qq_app_id) - 1);
    if ((env = getenv("QQ_APP_SECRET")))
        strncpy(config.qq_app_secret, env, sizeof(config.qq_app_secret) - 1);
    if ((env = getenv("TG_BOT_TOKEN")))
        strncpy(config.tg_bot_token, env, sizeof(config.tg_bot_token) - 1);
    if ((env = getenv("FEISHU_APP_ID")))
        strncpy(config.feishu_app_id, env, sizeof(config.feishu_app_id) - 1);
    if ((env = getenv("FEISHU_APP_SECRET")))
        strncpy(config.feishu_app_secret, env, sizeof(config.feishu_app_secret) - 1);
    if ((env = getenv("WECHAT_TOKEN")))
        strncpy(config.wechat_token, env, sizeof(config.wechat_token) - 1);
    if ((env = getenv("BRAVE_SEARCH_KEY")))
        strncpy(config.search_brave_key, env, sizeof(config.search_brave_key) - 1);
    if ((env = getenv("TAVILY_SEARCH_KEY")))
        strncpy(config.search_tavily_key, env, sizeof(config.search_tavily_key) - 1);

    /* Defaults */
    if (!config.llm_profile[0])
        strncpy(config.llm_profile, "openai", sizeof(config.llm_profile) - 1);
    if (!config.llm_timeout_ms[0])
        strncpy(config.llm_timeout_ms, "30000", sizeof(config.llm_timeout_ms) - 1);
    if (!config.llm_max_tokens[0])
        strncpy(config.llm_max_tokens, "4096", sizeof(config.llm_max_tokens) - 1);

    ESP_LOGI(TAG, "Data directory: %s", abs_data_dir);
    ESP_LOGI(TAG, "Log file: %s", log_path);

    /* Pass data dir to console_unix for socket path */
    setenv("ESP_AGENT_DATA_DIR", abs_data_dir, 1);
    ESP_LOGI(TAG, "Display: %s", display_enabled ? "enabled" : "disabled");
    ESP_LOGI(TAG, "LLM config: profile=%s model=%s base_url=%s",
             config.llm_profile,
             config.llm_model[0] ? config.llm_model : "(unset)",
             config.llm_base_url[0] ? config.llm_base_url : "(default)");
    ESP_LOGI(TAG, "Channels: QQ=%s TG=%s Feishu=%s WeChat=%s local_im=enabled",
             config.qq_app_id[0] ? "configured" : "off",
             config.tg_bot_token[0] ? "configured" : "off",
             config.feishu_app_id[0] ? "configured" : "off",
             config.wechat_token[0] ? "configured" : "off");

    /* ---- SDL2 display (optional) ---- */
    if (display_enabled) {
        esp_err_t d_err = display_hal_create(NULL, NULL, 0, 320, 240);
        if (d_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create display: %s", esp_err_to_name(d_err));
        }
    } else {
        ESP_LOGI(TAG, "SDL2 display skipped per config");
    }

    /* ---- storage paths ---- */
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

    /* ---- bootstrap agent ---- */
    esp_err_t err = app_claw_start(&config, &paths);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start app_claw: %s", esp_err_to_name(err));
        return 1;
    }

    /* ---- start emote engine (boot animation) ---- */
    err = app_claw_ui_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Emote engine failed (%s), using fallback boot text",
                 esp_err_to_name(err));
        if (display_enabled && display_hal_is_active()) {
            display_hal_begin_frame(true, 0x18E3);
            int w = display_hal_width();
            int h = display_hal_height();
            display_hal_draw_text_aligned(0, 16, w, 24,
                "esp-claw Simulator", 2,
                0xFFDF, false, 0,
                DISPLAY_HAL_TEXT_ALIGN_CENTER, DISPLAY_HAL_TEXT_VALIGN_TOP);
            display_hal_draw_line(40, 48, w - 40, 48, 0x52AA);
            display_hal_draw_text(16, 60, "Display: SDL2 320x240", 1,
                0xCE59, false, 0);
            display_hal_draw_text(16, 80, "Emote not available", 1,
                0xCE59, false, 0);
            display_hal_present();
            display_hal_end_frame();
        }
    }

    ESP_LOGI(TAG, "Desktop simulator running. Press Ctrl+C to stop.");
    ESP_LOGI(TAG, "CLI socket: %s/agent.sock", abs_data_dir);

    /* Keep main thread alive, handle display present and events */
    while (s_running) {
        if (display_hal_is_active()) {
            display_hal_present();
            vTaskDelay(pdMS_TO_TICKS(16)); /* ~60 Hz */
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    /* Cleanup */
    display_hal_destroy();
    {
        char sock_path[PATH_MAX];
        snprintf(sock_path, sizeof(sock_path), "%s/agent.sock", abs_data_dir);
        unlink(sock_path);
    }
    if (g_pid_file_path[0]) unlink(g_pid_file_path);

    ESP_LOGI(TAG, "Shutting down...");
    return 0;
}
