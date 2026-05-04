/*
 * esp_agent_cli.c — Management CLI tool for esp-agent daemon
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
# define PIPE_NAME  "\\\\.\\pipe\\esp-agent"
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

#define VERSION       "1.0.0"
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
    DWORD written;
    if (!WriteFile(conn, cmd, (DWORD)strlen(cmd), &written, NULL)) return false;
    if (!WriteFile(conn, "\n", 1, &written, NULL)) return false;

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
        fprintf(stderr, "Agent is not running. Start it with: esp-agent start\n");
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
    printf("=== esp-agent Configuration Wizard ===\n\n");

    char config_path[512];
    get_config_path(config_path, sizeof(config_path));

    /* Read existing config */
    cJSON *root = NULL;
    FILE *fp = fopen(config_path, "r");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        rewind(fp);
        char *buf = calloc(1, (size_t)sz + 1);
        if (buf && fread(buf, 1, (size_t)sz, fp) == (size_t)sz) {
            root = cJSON_Parse(buf);
        }
        free(buf);
        fclose(fp);
    }
    if (!root) root = cJSON_CreateObject();

    /* Ensure sections */
    cJSON *llm = cJSON_GetObjectItemCaseSensitive(root, "llm");
    if (!llm) { llm = cJSON_AddObjectToObject(root, "llm"); }
    cJSON *channels = cJSON_GetObjectItemCaseSensitive(root, "channels");
    if (!channels) { channels = cJSON_AddObjectToObject(root, "channels"); }
    cJSON *search = cJSON_GetObjectItemCaseSensitive(root, "search");
    if (!search) { search = cJSON_AddObjectToObject(root, "search"); }
    cJSON *display = cJSON_GetObjectItemCaseSensitive(root, "display");
    if (!display) { display = cJSON_AddObjectToObject(root, "display"); }

    char line[512];

    /* LLM */
    printf("LLM Configuration\n");
    printf("────────────────\n");

    printf("API Key: "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0]) cJSON_AddStringToObject(llm, "api_key", line);

    printf("Model (e.g. gpt-4o): "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0]) cJSON_AddStringToObject(llm, "model", line);

    printf("Profile [openai/anthropic/deepseek/dashscope/volcengine/minimax/custom]: "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0]) cJSON_AddStringToObject(llm, "profile", line);

    printf("Base URL (empty for default): "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0]) cJSON_AddStringToObject(llm, "base_url", line);

    /* Display */
    printf("\nDisplay (SDL2 window)\n");
    printf("─────────────────────\n");
    printf("Enable display? [Y/n]: "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    bool en = (line[0] == '\0' || line[0] == 'y' || line[0] == 'Y');
    cJSON_AddBoolToObject(display, "enabled", en ? cJSON_True : cJSON_False);

    /* Search */
    printf("\nSearch Configuration (optional)\n");
    printf("───────────────────────────────\n");
    printf("Brave Search API Key: "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0]) cJSON_AddStringToObject(search, "brave_key", line);

    printf("Tavily Search API Key: "); fflush(stdout);
    fgets(line, sizeof(line), stdin);
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0]) cJSON_AddStringToObject(search, "tavily_key", line);

    /* Write config */
    char *out = cJSON_Print(root);
    if (out) {
        char dir[512];
        get_data_dir(dir, sizeof(dir));
        platform_mkdir(dir);
        fp = fopen(config_path, "w");
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
    /* Try alongside esp-agent.exe, then PATH */
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
        fprintf(stderr, "Cannot find %s. Run 'esp-agent build' first.\n", AGENT_EXE);
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

    printf("Agent started (detached process)\n");
    printf("Data directory: %s\n", data_dir);

    /* Wait a moment for the agent to write its PID */
    Sleep(1500);

    /* Tail logs */
    return cmd_logs(0, NULL);

#else
    /* POSIX: use existing shell script approach */
    fprintf(stderr, "Use the esp-agent shell script on Linux.\n");
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

    printf("esp-agent v%s\n\n", VERSION);

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
        printf("Config: not found (run 'esp-agent config')\n");
    }

    /* Check data dir */
    char data_dir[512];
    get_data_dir(data_dir, sizeof(data_dir));
    printf("Data:   %s\n", data_dir);

    return 0;
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
        fputs(line, stdout);
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
            fputs(line, stdout);
        }
        sz = ftell(fp);
        fclose(fp);
        fflush(stdout);
        platform_sleep_ms(500);
    }
    return 0;
}

static int cmd_build(int argc, char **argv)
{
    (void)argc; (void)argv;

#if defined(_WIN32)
    printf("Building esp-claw-desktop...\n");
    platform_mkdir("build");
    int rc = system("cd build && cmake .. -G \"MinGW Makefiles\" -DCMAKE_BUILD_TYPE=Release && mingw32-make -j4");
    return rc == 0 ? 0 : 1;
#else
    printf("Building esp-claw-desktop...\n");
    platform_mkdir("build");
    int rc = system("cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)");
    return rc == 0 ? 0 : 1;
#endif
}

static int cmd_clean(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Cleaning build directory...\n");

#if defined(_WIN32)
    int rc = system("rmdir /s /q build 2>nul");
#else
    int rc = system("rm -rf build");
#endif
    (void)rc;
    printf("Done.\n");
    return 0;
}

static int cmd_service(int argc, char **argv)
{
    (void)argc; (void)argv;
#if defined(_WIN32)
    printf("Service management is not supported on Windows.\n");
    printf("Use 'esp-agent start' / 'esp-agent stop' instead.\n");
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
    {"build",    "Compile the binary (Release)",       cmd_build,   true},
    {"clean",    "Remove build/ directory",            cmd_clean,   true},
    {"service",  "Service management (Linux only)",    cmd_service, true},
    {"help",     "Show this help",                     cmd_help,    true},
    {NULL, NULL, NULL, false}
};

static const size_t s_cmd_count = sizeof(s_cmds) / sizeof(s_cmds[0]) - 1;

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("esp-agent v%s — CLI management tool\n\n", VERSION);
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

    /* Forward everything else to the agent's REPL */
    /* Build full command line from remaining args */
    char full_cmd[MAX_LINE];
    size_t pos = 0;
    for (int i = 1; i < argc && pos < sizeof(full_cmd) - 2; i++) {
        if (i > 1) full_cmd[pos++] = ' ';
        size_t len = strlen(argv[i]);
        if (pos + len >= sizeof(full_cmd) - 1) len = sizeof(full_cmd) - pos - 1;
        memcpy(full_cmd + pos, argv[i], len);
        pos += len;
    }
    full_cmd[pos] = '\0';

    return forward_to_agent(full_cmd);
}
