/*
 * console_unix.c — esp_console over Unix domain socket for desktop simulator
 *
 * Replaces ESP-IDF's UART/USB console transport.  A listening Unix socket at
 * ~/.esp-agent/agent.sock accepts sequential client connections; each receives
 * a single command, executes it via the esp_console command registry, writes
 * the response back, and closes — non-interactive one-shot RPC.
 *
 * This preserves the upstream esp-claw CLI command parsing/routing unchanged;
 * only the transport is replaced (serial character device → datagram socket).
 */
#include "esp_console.h"
#include "esp_log.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static const char *TAG = "console_unix";

#define CONSOLE_MAX_COMMANDS 64
#define CONSOLE_MAX_CMDLINE  2048

/* ---- command registry ---- */
static esp_console_cmd_t s_commands[CONSOLE_MAX_COMMANDS];
static size_t            s_command_count;
static bool              s_help_registered;

/* ---- socket state ---- */
static char        s_sock_path[256];
static int         s_listen_fd = -1;
static pthread_t   s_accept_thread;
static volatile bool s_accept_running;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static void resolve_default_socket_path(const char *hint)
{
    if (hint && hint[0]) {
        strncpy(s_sock_path, hint, sizeof(s_sock_path) - 1);
        return;
    }
    const char *data_dir = getenv("ESP_AGENT_DATA_DIR");
    if (data_dir) {
        snprintf(s_sock_path, sizeof(s_sock_path), "%s/agent.sock", data_dir);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(s_sock_path, sizeof(s_sock_path),
             "%s/.esp-agent/agent.sock", home ? home : "/tmp");
}

static void ensure_parent_dir(const char *path)
{
    char dir[256];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }
}

/*
 * Tokenise a command line into argc/argv in-place (modifies *line).
 * Handles double-quoted strings.
 */
static int tokenize(char *line, char **argv, int max_args)
{
    int  argc = 0;
    char *p   = line;

    while (*p && argc < max_args) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (*p == '"') {
            p++; /* skip open quote */
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') { *p = '\0'; p++; }
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    return argc;
}

/* ------------------------------------------------------------------ */
/*  Command registry API                                              */
/* ------------------------------------------------------------------ */

esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd)
{
    if (!cmd || !cmd->command || !cmd->func) {
        return ESP_ERR_INVALID_ARG;
    }

    /* allow re-registration (latest wins) */
    for (size_t i = 0; i < s_command_count; i++) {
        if (strcmp(s_commands[i].command, cmd->command) == 0) {
            s_commands[i] = *cmd;
            return ESP_OK;
        }
    }

    if (s_command_count >= CONSOLE_MAX_COMMANDS) {
        ESP_LOGE(TAG, "command table full (%u)", (unsigned)CONSOLE_MAX_COMMANDS);
        return ESP_ERR_NO_MEM;
    }

    s_commands[s_command_count++] = *cmd;
    return ESP_OK;
}

/* ---- built-in help ---- */
static int builtin_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Available commands:\r\n");
    for (size_t i = 0; i < s_command_count; i++) {
        printf("  %-20s %s\r\n",
               s_commands[i].command,
               s_commands[i].help ? s_commands[i].help : "");
    }
    return 0;
}

esp_err_t esp_console_register_help_command(void)
{
    if (s_help_registered) return ESP_OK;
    s_help_registered = true;
    const esp_console_cmd_t cmd = {
        .command = "help",
        .help    = "List all registered commands",
        .func    = builtin_help,
    };
    return esp_console_cmd_register(&cmd);
}

/* ------------------------------------------------------------------ */
/*  Argument splitting (public API used by cap_cli)                    */
/* ------------------------------------------------------------------ */

size_t esp_console_split_argv(char *line, char **argv, size_t argv_size)
{
    size_t argc = 0;
    char *p = line;

    if (!line || !argv || argv_size == 0) return 0;

    while (*p && argc < argv_size) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') { *p = '\0'; p++; }
        } else {
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    return argc;
}

/* ------------------------------------------------------------------ */
/*  Command execution (used by oneshot handler)                       */
/* ------------------------------------------------------------------ */

esp_err_t esp_console_run(const char *command_line, int *cmd_ret)
{
    if (!command_line || !cmd_ret) {
        return ESP_ERR_INVALID_ARG;
    }

    /* mutable copy */
    char line[CONSOLE_MAX_CMDLINE];
    strncpy(line, command_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    char *argv[64];
    int   argc = tokenize(line, argv, 64);

    if (argc == 0) {
        *cmd_ret = 0;
        return ESP_OK;
    }

    for (size_t i = 0; i < s_command_count; i++) {
        if (strcmp(s_commands[i].command, argv[0]) == 0) {
            *cmd_ret = s_commands[i].func(argc, argv);
            return ESP_OK;
        }
    }

    printf("Unknown command: %s\r\n", argv[0]);
    *cmd_ret = 1;
    return ESP_ERR_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/*  One-shot session — read one command, execute, return response     */
/* ------------------------------------------------------------------ */

static void oneshot_session(int client_fd)
{
    /* Redirect only stdout to the client socket so command output
       (printf) reaches the caller.  stderr is left alone — log output
       continues to the log file and/or journal, never mixed into
       command responses. */
    int saved_stdout = dup(STDOUT_FILENO);
    fflush(stdout);  /* drain any buffered startup messages to original fd */
    dup2(client_fd, STDOUT_FILENO);

    char buf[CONSOLE_MAX_CMDLINE];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        /* strip trailing CR / LF */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
        if (n > 0) {
            int cmd_ret;
            esp_console_run(buf, &cmd_ret);
        }
    }

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
}

/* ------------------------------------------------------------------ */
/*  Accept thread — serves one-shot clients sequentially              */
/* ------------------------------------------------------------------ */

static void *accept_thread(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "accept loop started on %s", s_sock_path);
    s_accept_running = true;

    while (s_accept_running) {
        int client_fd = accept(s_listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (s_accept_running)
                ESP_LOGE(TAG, "accept: %s", strerror(errno));
            break;
        }

        oneshot_session(client_fd);
        close(client_fd);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Public API: create REPL                                           */
/* ------------------------------------------------------------------ */

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev_config,
                                    const esp_console_repl_config_t *repl_config,
                                    esp_console_repl_t **ret_repl)
{
    (void)repl_config;
    if (!ret_repl) return ESP_ERR_INVALID_ARG;

    resolve_default_socket_path(dev_config ? dev_config->socket_path : NULL);
    ensure_parent_dir(s_sock_path);

    /* Remove any stale socket */
    unlink(s_sock_path);

    s_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        ESP_LOGE(TAG, "socket(): %s", strerror(errno));
        return ESP_FAIL;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_sock_path, sizeof(addr.sun_path) - 1);

    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind(%s): %s", s_sock_path, strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return ESP_FAIL;
    }

    if (listen(s_listen_fd, 4) < 0) {
        ESP_LOGE(TAG, "listen(): %s", strerror(errno));
        close(s_listen_fd);
        unlink(s_sock_path);
        s_listen_fd = -1;
        return ESP_FAIL;
    }

    *ret_repl = calloc(1, 1); /* non-NULL opaque handle */
    if (!*ret_repl) {
        close(s_listen_fd);
        unlink(s_sock_path);
        s_listen_fd = -1;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Unix socket ready at %s", s_sock_path);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API: start REPL (non‑blocking)                             */
/* ------------------------------------------------------------------ */

esp_err_t esp_console_start_repl(esp_console_repl_t *repl)
{
    (void)repl;
    if (s_listen_fd < 0) {
        ESP_LOGE(TAG, "start_repl: socket not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    int rc = pthread_create(&s_accept_thread, NULL, accept_thread, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "pthread_create: %s", strerror(rc));
        return ESP_FAIL;
    }
    pthread_detach(s_accept_thread);

    ESP_LOGI(TAG, "Command socket started (background thread)");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Stubs for unused USB backends                                     */
/* ------------------------------------------------------------------ */

esp_err_t esp_console_new_repl_usb_serial_jtag(
        const esp_console_dev_usb_serial_jtag_config_t *dev_config,
        const esp_console_repl_config_t *repl_config,
        esp_console_repl_t **ret_repl)
{
    (void)dev_config; (void)repl_config; (void)ret_repl;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_console_new_repl_usb_cdc(
        const esp_console_dev_usb_cdc_config_t *dev_config,
        const esp_console_repl_config_t *repl_config,
        esp_console_repl_t **ret_repl)
{
    (void)dev_config; (void)repl_config; (void)ret_repl;
    return ESP_ERR_NOT_SUPPORTED;
}
