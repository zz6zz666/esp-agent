/*
 * crush_claw_cli.c — Management CLI tool for Crush Claw daemon
 *
 * On Linux this is compiled as a shell-script frontend; on Windows it's a
 * compiled C tool that communicates with the daemon via Named Pipe.
 *
 * Management commands (handled locally):
 *   config, start, stop, restart, status, logs, build, clean, service
 *
 * Everything else is forwarded to the daemon's REPL over the pipe/socket.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <direct.h>
# include <io.h>
# include <tlhelp32.h>
# define PIPE_NAME  "\\\\.\\pipe\\crush-claw"
#else
# include <errno.h>
# include <signal.h>
# include <sys/socket.h>
# include <sys/stat.h>
# include <sys/un.h>
# include <unistd.h>
#endif

#include "cJSON.h"
#include "platform.h"

#define VERSION       "1.1.0"
#define MAX_LINE      32768
#define AGENT_EXE     "esp-claw-desktop" PLATFORM_EXE_SUFFIX
#define DEFAULTS_DIR  "defaults"
#define SKILLS_DIR    "skills"

/* ---- helpers ---- */

static void get_data_dir(char *buf, size_t size)
{
    platform_get_data_dir(buf, size);
}

static void get_pid_path(char *buf, size_t size)
{
    get_data_dir(buf, size);
    size_t len = strlen(buf);
    snprintf(buf + len, size - len, "%cagent.pid", PLATFORM_PATH_SEP);
}

static int cmd_logs(int argc, char **argv);

static void get_sock_path(char *buf, size_t size)
{
    get_data_dir(buf, size);
    size_t len = strlen(buf);
    snprintf(buf + len, size - len, "%cagent.sock", PLATFORM_PATH_SEP);
}

static void get_log_path(char *buf, size_t size)
{
    get_data_dir(buf, size);
    size_t len = strlen(buf);
    snprintf(buf + len, size - len, "%cagent.log", PLATFORM_PATH_SEP);
}

static void get_config_path(char *buf, size_t size)
{
    get_data_dir(buf, size);
    size_t len = strlen(buf);
    snprintf(buf + len, size - len, "%cconfig.json", PLATFORM_PATH_SEP);
}

/* Interactive prompt helpers for cmd_config */
static void read_line(char *line, size_t size)
{
    fgets(line, (int)size, stdin);
    line[strcspn(line, "\r\n")] = '\0';
}

static void prompt_str(const char *prompt, const char *default_val,
                       char *out, size_t out_size)
{
    if (default_val && default_val[0])
        printf("%s [%s]: ", prompt, default_val);
    else
        printf("%s: ", prompt);
    fflush(stdout);
    read_line(out, out_size);
    if (out[0] == '\0' && default_val) {
        strncpy(out, default_val, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

static bool prompt_yes_no(const char *prompt, bool default_yes)
{
    printf("%s [%s]: ", prompt, default_yes ? "Y/n" : "y/N");
    fflush(stdout);
    char line[32];
    read_line(line, sizeof(line));
    if (line[0] == '\0') return default_yes;
    return line[0] == 'y' || line[0] == 'Y';
}

/* ---- PID helpers ---- */

#if defined(_WIN32)

static bool pid_file_exists(void)
{
    char path[512];
    get_pid_path(path, sizeof(path));
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static DWORD read_pid(void)
{
    char path[512];
    get_pid_path(path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    DWORD pid = 0;
    fscanf(fp, "%lu", &pid);
    fclose(fp);
    return pid;
}

static bool is_process_running(DWORD pid)
{
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

#else

static bool pid_file_exists(void)
{
    char path[512];
    get_pid_path(path, sizeof(path));
    return access(path, F_OK) == 0;
}

static pid_t read_pid(void)
{
    char path[512];
    get_pid_path(path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    pid_t pid = 0;
    fscanf(fp, "%d", &pid);
    fclose(fp);
    return pid;
}

static bool is_process_running(pid_t pid)
{
    if (pid <= 0) return false;
    return kill(pid, 0) == 0;
}

#endif

/* ---- IPC: connect to daemon ---- */

#if defined(_WIN32)

static HANDLE connect_to_agent(void)
{
    HANDLE hPipe = CreateFileA(PIPE_NAME,
                               GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);
    return hPipe;
}

static void disconnect_from_agent(HANDLE hPipe)
{
    if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
}

static bool send_command(HANDLE conn, const char *cmd, char *resp, size_t resp_size)
{
    /* Build cmd + newline into one buffer, write as single message.
     * The server reads exactly one message; splitting across two
     * WriteFile calls leaves a stale newline message in the pipe. */
    size_t cmd_len = strlen(cmd);
    char *full_cmd = malloc(cmd_len + 2);
    if (!full_cmd) return false;
    memcpy(full_cmd, cmd, cmd_len);
    full_cmd[cmd_len] = '\n';
    full_cmd[cmd_len + 1] = '\0';

    DWORD written;
    BOOL ok = WriteFile(conn, full_cmd, (DWORD)(cmd_len + 1), &written, NULL);
    free(full_cmd);
    if (!ok) return false;

    /* Read response */
    char buf[8192];
    DWORD n;
    size_t total = 0;
    while (total < resp_size - 1) {
        if (!ReadFile(conn, buf, (DWORD)sizeof(buf) - 1, &n, NULL) || n == 0)
            break;
        buf[n] = '\0';
        size_t copy = (size_t)n;
        if (total + copy >= resp_size) copy = resp_size - total - 1;
        memcpy(resp + total, buf, copy);
        total += copy;
    }
    resp[total] = '\0';
    return total > 0;
}

#else

typedef int agent_conn_t;
#define INVALID_CONN (-1)

static agent_conn_t connect_to_agent(void)
{
    char sock_path[512];
    get_sock_path(sock_path, sizeof(sock_path));

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return INVALID_CONN;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return INVALID_CONN;
    }
    return fd;
}

static void disconnect_from_agent(agent_conn_t conn)
{
    if (conn >= 0) close(conn);
}

static bool send_command(agent_conn_t conn, const char *cmd, char *resp, size_t resp_size)
{
    write(conn, cmd, strlen(cmd));
    write(conn, "\n", 1);

    size_t total = 0;
    char buf[8192];
    ssize_t n;
    while (total < resp_size - 1) {
        n = read(conn, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        size_t copy = (size_t)n;
        if (total + copy >= resp_size) copy = resp_size - total - 1;
        memcpy(resp + total, buf, copy);
        total += copy;
    }
    resp[total] = '\0';
    return total > 0;
}

#endif

/* ---- Forward a command to the daemon over IPC ---- */
static int forward_to_agent(const char *cmd)
{
    char resp[MAX_LINE];
    void *conn = (void *)connect_to_agent();
#if defined(_WIN32)
    if (conn == INVALID_HANDLE_VALUE) {
#else
    if ((int)(intptr_t)conn == INVALID_CONN) {
#endif
        fprintf(stderr, "Agent is not running. Start it with: crush-claw start\n");
        return 1;
    }

    if (!send_command(conn, cmd, resp, sizeof(resp))) {
        fprintf(stderr, "Failed to send command to agent.\n");
        disconnect_from_agent(conn);
        return 1;
    }

    fputs(resp, stdout);
    disconnect_from_agent(conn);
    return 0;
}

/* ---- Management commands ---- */

static int cmd_config(int argc, char **argv)
{
    (void)argc; (void)argv;
    char config_path[512];
    get_config_path(config_path, sizeof(config_path));

    printf("========================================\n");
    printf("  Crush Claw Configuration Wizard\n");
    printf("========================================\n");
    printf("\nThis wizard will create %s\n", config_path);
    printf("Press Enter to accept default values (shown in brackets).\n\n");

    /* ================================================================
     * Section 1 — LLM Model Provider
     * ================================================================ */
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  1. LLM Model Provider\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("  Select a provider by number:\n\n");
    printf("  ── Preset providers (fixed URLs) ──\n");
    printf("  1. DeepSeek          (Anthropic)   deepseek-v4-flash\n");
    printf("  2. 阿里云百炼        (OpenAI)      qwen3.6-flash\n");
    printf("  3. 阿里云百炼 Coding  (Anthropic)   qwen3.6-plus\n");
    printf("  4. 火山引擎           (OpenAI)      doubao-seed-2-0-lite-260215\n");
    printf("  5. 火山引擎 Coding    (Anthropic)   doubao-seed-2.0-pro\n");
    printf("  6. MiniMax            (Anthropic)   MiniMax-M2.7\n\n");
    printf("  ── Custom providers ──\n");
    printf("  7. OpenAI             (OpenAI)      gpt-4o\n");
    printf("  8. OpenAI Compatible  (OpenAI)      custom URL\n");
    printf("  9. Anthropic          (Anthropic)   claude-sonnet-4-20250514\n");
    printf(" 10. Anthropic Compat.  (Anthropic)   custom URL\n\n");

    char line[512];
    printf("Provider number [7]: "); fflush(stdout);
    read_line(line, sizeof(line));
    int provider = line[0] ? atoi(line) : 7;
    if (provider < 1 || provider > 10) provider = 7;

    char profile[64]      = "";
    char base_url[512]    = "";
    char default_model[128] = "";
    bool ask_url = false;

    switch (provider) {
        case 1: strcpy(profile, "anthropic");
                strcpy(base_url, "https://api.deepseek.com/anthropic");
                strcpy(default_model, "deepseek-v4-flash"); break;
        case 2: strcpy(profile, "custom_openai_compatible");
                strcpy(base_url, "https://dashscope.aliyuncs.com/compatible-mode/v1");
                strcpy(default_model, "qwen3.6-flash"); break;
        case 3: strcpy(profile, "anthropic");
                strcpy(base_url, "https://coding.dashscope.aliyuncs.com/apps/anthropic");
                strcpy(default_model, "qwen3.6-plus"); break;
        case 4: strcpy(profile, "custom_openai_compatible");
                strcpy(base_url, "https://ark.cn-beijing.volces.com/api/v3");
                strcpy(default_model, "doubao-seed-2-0-lite-260215"); break;
        case 5: strcpy(profile, "anthropic");
                strcpy(base_url, "https://ark.cn-beijing.volces.com/api/coding");
                strcpy(default_model, "doubao-seed-2.0-pro"); break;
        case 6: strcpy(profile, "anthropic");
                strcpy(base_url, "https://api.minimaxi.com/anthropic");
                strcpy(default_model, "MiniMax-M2.7"); break;
        case 7: strcpy(profile, "openai");
                strcpy(default_model, "gpt-4o"); break;
        case 8: strcpy(profile, "custom_openai_compatible");
                strcpy(default_model, "gpt-4o"); ask_url = true; break;
        case 9: strcpy(profile, "anthropic");
                strcpy(default_model, "claude-sonnet-4-20250514"); break;
        case 10: strcpy(profile, "anthropic");
                 strcpy(default_model, "claude-sonnet-4-20250514"); ask_url = true; break;
    }

    printf("\n");

    /* API key — show existing env var if set */
    const char *env_key = getenv("LLM_API_KEY");
    prompt_str("LLM API key", env_key, line, sizeof(line));
    char api_key[512]; strcpy(api_key, line);

    prompt_str("LLM model", default_model, line, sizeof(line));
    char model[256]; strcpy(model, line);

    /* Custom base URL for compatible types */
    if (ask_url) {
        printf("LLM base URL (required): "); fflush(stdout);
        read_line(line, sizeof(line));
        strcpy(base_url, line);
    } else if (base_url[0]) {
        /* Preset had a base_url; option to override */
        char override_url[512];
        prompt_str("LLM base URL", base_url, override_url, sizeof(override_url));
        strcpy(base_url, override_url);
    }

    printf("\n");

    /* ================================================================
     * Section 2 — IM Channels
     * ================================================================ */
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  2. IM Channels (消息渠道接入)\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("  The agent can receive and reply to messages through\n");
    printf("  various IM platforms. Select which channels to enable.\n");
    printf("  You will be prompted for credentials for each enabled channel.\n\n");

    printf("  [Local IM / Web Chat] — Built-in web-based IM, always available.\n");

    bool qq_enabled      = prompt_yes_no("Enable QQ Bot?", false);
    char qq_app_id[128]  = "";
    char qq_app_secret[256] = "";
    if (qq_enabled) {
        printf("  QQ Bot requires App ID + App Secret from https://q.qq.com\n");
        prompt_str("QQ App ID", NULL, qq_app_id, sizeof(qq_app_id));
        prompt_str("QQ App Secret", NULL, qq_app_secret, sizeof(qq_app_secret));
    }

    bool tg_enabled      = prompt_yes_no("Enable Telegram Bot?", false);
    char tg_bot_token[256] = "";
    if (tg_enabled) {
        printf("  Telegram Bot requires a Bot Token from @BotFather\n");
        prompt_str("Bot Token", NULL, tg_bot_token, sizeof(tg_bot_token));
    }

    bool feishu_enabled     = prompt_yes_no("Enable Feishu/Lark?", false);
    char feishu_app_id[128]  = "";
    char feishu_app_secret[256] = "";
    if (feishu_enabled) {
        printf("  Feishu/Lark requires App ID + App Secret from Feishu Open Platform\n");
        prompt_str("App ID", NULL, feishu_app_id, sizeof(feishu_app_id));
        prompt_str("App Secret", NULL, feishu_app_secret, sizeof(feishu_app_secret));
    }

    bool wechat_enabled       = prompt_yes_no("Enable WeChat (微信)?", false);
    char wechat_token[256]     = "";
    char wechat_base_url[256]  = "";
    char wechat_cdn_url[256]   = "";
    char wechat_account_id[64] = "";
    if (wechat_enabled) {
        printf("  WeChat requires a token and account configuration\n");
        prompt_str("Token", NULL, wechat_token, sizeof(wechat_token));
        prompt_str("Base URL", "https://ilinkai.weixin.qq.com", wechat_base_url, sizeof(wechat_base_url));
        prompt_str("CDN Base URL", "https://novac2c.cdn.weixin.qq.com/c2c", wechat_cdn_url, sizeof(wechat_cdn_url));
        prompt_str("Account ID", "default", wechat_account_id, sizeof(wechat_account_id));
    }

    printf("\n");

    /* ================================================================
     * Section 3 — Web Search
     * ================================================================ */
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  3. Web Search (optional)\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    printf("  API keys for web search providers (optional).\n");
    printf("  Leave empty to skip.\n\n");

    prompt_str("Brave Search API key", "", line, sizeof(line));
    char brave_key[256]; strcpy(brave_key, line);

    prompt_str("Tavily Search API key", "", line, sizeof(line));
    char tavily_key[256]; strcpy(tavily_key, line);

    printf("\n");

    /* ================================================================
     * Section 4 — Display
     * ================================================================ */
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  4. Display\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    bool display_enabled = prompt_yes_no("Enable simulated LCD display window?", true);

    printf("\n");

    /* ================================================================
     * Build config.json
     * ================================================================ */
    char dir[512];
    get_data_dir(dir, sizeof(dir));
    platform_mkdir(dir);

    cJSON *root = cJSON_CreateObject();

    cJSON *llm = cJSON_AddObjectToObject(root, "llm");
    cJSON_AddStringToObject(llm, "api_key", api_key);
    cJSON_AddStringToObject(llm, "model", model);
    cJSON_AddStringToObject(llm, "profile", profile);
    cJSON_AddStringToObject(llm, "base_url", base_url);
    cJSON_AddStringToObject(llm, "auth_type", "");
    cJSON_AddStringToObject(llm, "timeout_ms", "120000");
    cJSON_AddStringToObject(llm, "max_tokens", "8192");

    cJSON *channels = cJSON_AddObjectToObject(root, "channels");

    cJSON *local_im = cJSON_AddObjectToObject(channels, "local_im");
    cJSON_AddBoolToObject(local_im, "enabled", cJSON_True);

    cJSON *qq = cJSON_AddObjectToObject(channels, "qq");
    cJSON_AddBoolToObject(qq, "enabled", qq_enabled ? cJSON_True : cJSON_False);
    cJSON_AddStringToObject(qq, "app_id", qq_app_id);
    cJSON_AddStringToObject(qq, "app_secret", qq_app_secret);

    cJSON *tg = cJSON_AddObjectToObject(channels, "telegram");
    cJSON_AddBoolToObject(tg, "enabled", tg_enabled ? cJSON_True : cJSON_False);
    cJSON_AddStringToObject(tg, "bot_token", tg_bot_token);

    cJSON *feishu = cJSON_AddObjectToObject(channels, "feishu");
    cJSON_AddBoolToObject(feishu, "enabled", feishu_enabled ? cJSON_True : cJSON_False);
    cJSON_AddStringToObject(feishu, "app_id", feishu_app_id);
    cJSON_AddStringToObject(feishu, "app_secret", feishu_app_secret);

    cJSON *wechat = cJSON_AddObjectToObject(channels, "wechat");
    cJSON_AddBoolToObject(wechat, "enabled", wechat_enabled ? cJSON_True : cJSON_False);
    cJSON_AddStringToObject(wechat, "token", wechat_token);
    cJSON_AddStringToObject(wechat, "base_url", wechat_base_url);
    cJSON_AddStringToObject(wechat, "cdn_base_url", wechat_cdn_url);
    cJSON_AddStringToObject(wechat, "account_id", wechat_account_id);

    cJSON *search = cJSON_AddObjectToObject(root, "search");
    cJSON_AddStringToObject(search, "brave_key", brave_key);
    cJSON_AddStringToObject(search, "tavily_key", tavily_key);

    cJSON *display = cJSON_AddObjectToObject(root, "display");
    cJSON_AddBoolToObject(display, "enabled", display_enabled ? cJSON_True : cJSON_False);
    cJSON_AddNumberToObject(display, "lcd_width", 480);
    cJSON_AddNumberToObject(display, "lcd_height", 480);
    cJSON_AddStringToObject(display, "emote_text", "");

    cJSON *session = cJSON_AddObjectToObject(root, "session");
    cJSON_AddStringToObject(session, "context_token_budget", "96256");
    cJSON_AddStringToObject(session, "max_message_chars", "8192");
    cJSON_AddStringToObject(session, "compress_threshold_percent", "80");

    /* Write config */
    char *out = cJSON_Print(root);
    if (out) {
        FILE *fp = fopen(config_path, "wb");
        if (fp) {
            fputs(out, fp);
            fclose(fp);
            printf("\nConfiguration saved to %s\n", config_path);
        } else {
            fprintf(stderr, "Failed to write config to %s\n", config_path);
        }
        free(out);
    }
    cJSON_Delete(root);

    /* Summary */
    printf("\n========================================\n");
    printf("  Configuration complete!\n");
    printf("========================================\n\n");
    printf("Summary:\n");
    printf("  LLM:        %s / %s\n", profile, model);
    printf("  Local IM:   enabled (always on)\n");
    printf("  QQ:         %s\n", qq_enabled ? "enabled" : "disabled");
    printf("  Telegram:   %s\n", tg_enabled ? "enabled" : "disabled");
    printf("  Feishu:     %s\n", feishu_enabled ? "enabled" : "disabled");
    printf("  WeChat:     %s\n", wechat_enabled ? "enabled" : "disabled");
    printf("  Display:    %s\n", display_enabled ? "enabled" : "disabled");
    printf("\nRun 'crush-claw start' to launch the agent.\n");

    return 0;
}

static int cmd_start(int argc, char **argv)
{
    (void)argc; (void)argv;

    /* Check if already running */
    if (pid_file_exists()) {
        DWORD pid = read_pid();
        if (is_process_running(pid)) {
            printf("Agent is already running (PID %lu)\n", (unsigned long)pid);
            return 0;
        }
    }

    /* Find the agent binary */
    char exe_path[512];

#if defined(_WIN32)
    /* Try alongside crush-claw.exe, then PATH */
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    char *slash = strrchr(exe_path, '\\');
    if (slash) {
        snprintf(slash + 1, sizeof(exe_path) - (size_t)(slash + 1 - exe_path),
                 "%s", AGENT_EXE);
    }

    if (GetFileAttributesA(exe_path) == INVALID_FILE_ATTRIBUTES) {
        /* Try current directory */
        snprintf(exe_path, sizeof(exe_path), ".\\%s", AGENT_EXE);
    }
    if (GetFileAttributesA(exe_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "Cannot find %s. Run 'crush-claw build' first.\n", AGENT_EXE);
        return 1;
    }

    char data_dir[512];
    get_data_dir(data_dir, sizeof(data_dir));
    platform_mkdir(data_dir);

    char pid_path[512];
    get_pid_path(pid_path, sizeof(pid_path));

    /* Build command line */
    char cmdline[2048];
    snprintf(cmdline, sizeof(cmdline),
             "\"%s\" --daemon --data-dir \"%s\" --pid-file \"%s\"",
             exe_path, data_dir, pid_path);

    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to launch agent (error %lu)\n", GetLastError());
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    printf("Agent started (PID %lu)\n", GetCurrentProcessId());

    /* Wait a moment for the agent to write its PID */
    Sleep(1000);

    /* Verify PID file exists */
    if (pid_file_exists()) {
        char pid_path[512];
        get_pid_path(pid_path, sizeof(pid_path));
        printf("PID file: %s\n", pid_path);
    } else {
        printf("Warning: PID file not yet written, agent may still be starting.\n");
    }

    /* Auto-tail logs (Linux-style behavior) */
    printf("\n");
    return cmd_logs(0, NULL);

#else
    /* POSIX: use existing shell script approach */
    fprintf(stderr, "Use the crush-claw shell script on Linux.\n");
    return 1;
#endif
}

static int cmd_stop(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (!pid_file_exists()) {
        printf("Agent is not running.\n");
        return 0;
    }

#if defined(_WIN32)
    DWORD pid = read_pid();
    if (pid == 0 || !is_process_running(pid)) {
        printf("Agent is not running (stale PID file cleaned up).\n");
        char pid_path[512];
        get_pid_path(pid_path, sizeof(pid_path));
        _unlink(pid_path);
        return 0;
    }

    /* Try graceful shutdown via pipe first */
    HANDLE hPipe = connect_to_agent();
    if (hPipe != INVALID_HANDLE_VALUE) {
        char resp[256];
        send_command(hPipe, "mcp_server --disable", resp, sizeof(resp));
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        Sleep(500);
    }

    /* Force terminate if still running */
    if (is_process_running(pid)) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
            printf("Agent stopped (PID %lu)\n", (unsigned long)pid);
        }
    }

    char pid_path[512];
    get_pid_path(pid_path, sizeof(pid_path));
    _unlink(pid_path);
    return 0;

#else
    pid_t pid = read_pid();
    if (pid <= 0 || !is_process_running(pid)) {
        printf("Agent is not running.\n");
        return 0;
    }
    if (kill(pid, SIGTERM) == 0) {
        printf("Sent SIGTERM to agent (PID %d)\n", pid);
    } else {
        fprintf(stderr, "Failed to stop agent: %s\n", strerror(errno));
        return 1;
    }
    return 0;
#endif
}

static int cmd_restart(int argc, char **argv)
{
    cmd_stop(0, NULL);
    platform_sleep_ms(1000);
    return cmd_start(argc, argv);
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("crush-claw v%s\n\n", VERSION);

    /* Check daemon */
    if (pid_file_exists()) {
        DWORD pid = read_pid();
        if (is_process_running(pid)) {
            printf("Agent: RUNNING (PID %lu)\n", (unsigned long)pid);
        } else {
            printf("Agent: STOPPED (stale PID %lu)\n", (unsigned long)pid);
        }
    } else {
        printf("Agent: STOPPED\n");
    }

    /* Check socket */
#if defined(_WIN32)
    HANDLE hPipe = connect_to_agent();
    if (hPipe != INVALID_HANDLE_VALUE) {
        printf("Pipe:  CONNECTED\n");
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    } else {
        printf("Pipe:  not available\n");
    }
#else
    char sock_path[512];
    get_sock_path(sock_path, sizeof(sock_path));
    struct stat st;
    if (stat(sock_path, &st) == 0) {
        printf("Socket: %s (%ld bytes)\n", sock_path, (long)st.st_size);
    } else {
        printf("Socket: not found\n");
    }
#endif

    /* Check config */
    char config_path[512];
    get_config_path(config_path, sizeof(config_path));
    if (platform_file_exists(config_path)) {
        printf("Config: %s\n", config_path);
    } else {
        printf("Config: not found (run 'crush-claw config')\n");
    }

    /* Check data dir */
    char data_dir[512];
    get_data_dir(data_dir, sizeof(data_dir));
    printf("Data:   %s\n", data_dir);

    return 0;
}

static void print_log_colored(const char *line)
{
    const char *color = "";
    if (line[0] == '[') {
        const char *b = strchr(line + 1, '[');
        if (b && b[1] && b[2] == ']') {
            switch (b[1]) {
            case 'E': color = "\033[31m"; break;
            case 'W': color = "\033[33m"; break;
            case 'I': color = "\033[32m"; break;
            case 'D': color = "\033[37m"; break;
            }
        }
    }
    printf("%s%s\033[0m", color, line);
    fflush(stdout);
}

static int cmd_logs(int argc, char **argv)
{
    (void)argc; (void)argv;
    char log_path[512];
    get_log_path(log_path, sizeof(log_path));

    FILE *fp = fopen(log_path, "r");
    if (!fp) {
        fprintf(stderr, "Log file not found: %s\n", log_path);
        return 1;
    }

    /* Seek to near end and tail */
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    long start = sz > 16384 ? sz - 16384 : 0;
    fseek(fp, start, SEEK_SET);
    if (start > 0) fgets(NULL, 0, fp); /* skip partial line */

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        print_log_colored(line);
    }
    fclose(fp);

    /* Follow mode */
    printf("--- Following %s (Ctrl+C to exit) ---\n", log_path);
    fflush(stdout);

    while (1) {
        fp = fopen(log_path, "r");
        if (!fp) break;
        fseek(fp, sz, SEEK_SET);
        while (fgets(line, sizeof(line), fp)) {
            print_log_colored(line);
        }
        sz = ftell(fp);
        fclose(fp);
        platform_sleep_ms(500);
    }
    return 0;
}

static int cmd_service(int argc, char **argv)
{
    (void)argc; (void)argv;
#if defined(_WIN32)
    printf("Service management is not supported on Windows.\n");
    printf("Use 'crush-claw start' / 'crush-claw stop' instead.\n");
#else
    printf("Use systemctl --user for service management.\n");
#endif
    return 0;
}

static int cmd_help(int argc, char **argv);

/* ---- main ---- */

typedef struct {
    const char *name;
    const char *desc;
    int (*func)(int argc, char **argv);
    bool local_only;  /* true = management command, false = forward to agent */
} cmd_entry_t;

static const cmd_entry_t s_cmds[] = {
    {"config",   "Interactive setup wizard",           cmd_config,  true},
    {"start",    "Start agent as background daemon",   cmd_start,   true},
    {"stop",     "Graceful shutdown",                  cmd_stop,    true},
    {"restart",  "Stop then start the agent",          cmd_restart, true},
    {"status",   "Check if agent is running",          cmd_status,  true},
    {"logs",     "Tail the agent log file",            cmd_logs,    true},
    {"service",  "Service management (Linux only)",    cmd_service, true},
    {"help",     "Show this help",                     cmd_help,    true},
    {NULL, NULL, NULL, false}
};

static const size_t s_cmd_count = sizeof(s_cmds) / sizeof(s_cmds[0]) - 1;

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("crush-claw v%s — CLI management tool\n\n", VERSION);
    printf("Management commands (handled locally):\n");
    for (size_t i = 0; i < s_cmd_count; i++) {
        printf("  %-12s %s\n", s_cmds[i].name, s_cmds[i].desc);
    }

    printf("\nAgent commands (forwarded to daemon REPL):\n");
    printf("  Everything else is sent to the agent's CLI over IPC.\n");
    printf("  Examples: ask, cap, lua, session, display, skill, etc.\n");

    /* Try to get agent help */
    void *conn = connect_to_agent();
#if defined(_WIN32)
    bool connected = (conn != INVALID_HANDLE_VALUE);
#else
    bool connected = ((int)(intptr_t)conn != INVALID_CONN);
#endif
    if (connected) {
        printf("\n--- Agent REPL Help ---\n");
        char resp[MAX_LINE];
        if (send_command(conn, "help", resp, sizeof(resp))) {
            fputs(resp, stdout);
        }
        disconnect_from_agent(conn);
    }

    return 0;
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    /* Enable UTF-8 output on Windows console */
    SetConsoleOutputCP(CP_UTF8);
    /* Enable ANSI escape codes on Windows 10+ */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif

    if (argc < 2) {
        cmd_help(0, NULL);
        return 0;
    }

    const char *cmd = argv[1];

    /* Check if it's a local management command */
    for (size_t i = 0; i < s_cmd_count; i++) {
        if (strcmp(cmd, s_cmds[i].name) == 0) {
            return s_cmds[i].func(argc - 1, argv + 1);
        }
    }

    /* Forward everything else to the agent's REPL.
     * On Windows, argv is in the system code page (e.g. GBK); convert
     * each argument to UTF-8 so the agent / LLM receive valid Unicode. */
    char full_cmd[MAX_LINE];
    size_t pos = 0;
    for (int i = 1; i < argc && pos < sizeof(full_cmd) - 2; i++) {
        if (i > 1) full_cmd[pos++] = ' ';

#if defined(_WIN32)
        /* ACP → UTF-16 → UTF-8 conversion */
        int wlen = MultiByteToWideChar(CP_ACP, 0, argv[i], -1, NULL, 0);
        if (wlen > 0) {
            wchar_t *wbuf = malloc((size_t)wlen * sizeof(wchar_t));
            if (wbuf) {
                MultiByteToWideChar(CP_ACP, 0, argv[i], -1, wbuf, wlen);
                int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1,
                                               NULL, 0, NULL, NULL);
                if (ulen > 0) {
                    char *ubuf = malloc((size_t)ulen);
                    if (ubuf) {
                        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1,
                                           ubuf, ulen, NULL, NULL);
                        size_t len = strlen(ubuf);
                        if (pos + len >= sizeof(full_cmd) - 1)
                            len = sizeof(full_cmd) - pos - 1;
                        memcpy(full_cmd + pos, ubuf, len);
                        pos += len;
                        free(ubuf);
                    }
                }
                free(wbuf);
            }
        }
#else
        size_t len = strlen(argv[i]);
        if (pos + len >= sizeof(full_cmd) - 1) len = sizeof(full_cmd) - pos - 1;
        memcpy(full_cmd + pos, argv[i], len);
        pos += len;
#endif
    }
    full_cmd[pos] = '\0';

    return forward_to_agent(full_cmd);
}
