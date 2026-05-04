/*
 * esp_console.h — ESP-IDF console stub for desktop simulator
 *
 * Replaces the hardware UART/USB console transport with a Unix domain socket
 * so the es p-claw CLI works on desktop Linux.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- REPL handle (opaque) ---- */
typedef struct esp_console_repl *esp_console_repl_t;

/* ---- REPL configuration ---- */
typedef struct {
    const char *prompt;
    size_t      task_stack_size;
    size_t      max_cmdline_length;
} esp_console_repl_config_t;

#define ESP_CONSOLE_REPL_CONFIG_DEFAULT() \
    { .prompt = "app> ", .task_stack_size = 8192, .max_cmdline_length = 512 }

/* ---- UART device config (repurposed: .channel holds the socket path) ---- */
typedef struct {
    const char *socket_path;   /* Unix socket path (our extension) */
    int         channel;       /* unused */
    int         baud_rate;     /* unused */
    int         tx_gpio_num;   /* unused */
    int         rx_gpio_num;   /* unused */
} esp_console_dev_uart_config_t;

#define ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT() \
    { .socket_path = NULL, .channel = 0, .baud_rate = 115200, .tx_gpio_num = -1, .rx_gpio_num = -1 }

/* ---- USB serial JTAG / CDC config stubs (not used, but referenced by #if) ---- */
typedef struct { int dummy; } esp_console_dev_usb_serial_jtag_config_t;
typedef struct { int dummy; } esp_console_dev_usb_cdc_config_t;

#define ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT() { .dummy = 0 }
#define ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT()            { .dummy = 0 }

/* ---- Command definition ---- */
typedef int (*esp_console_cmd_func_t)(int argc, char **argv);

typedef struct {
    const char            *command;
    const char            *help;
    const char            *hint;       /* optional — one-line usage */
    esp_console_cmd_func_t func;
    void                  *argtable;   /* optional — argtable3 integration */
} esp_console_cmd_t;

/* ---- API ---- */
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);
esp_err_t esp_console_register_help_command(void);
esp_err_t esp_console_run(const char *command_line, int *cmd_ret);

size_t esp_console_split_argv(char *line, char **argv, size_t argv_size);

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev_config,
                                    const esp_console_repl_config_t *repl_config,
                                    esp_console_repl_t **ret_repl);
esp_err_t esp_console_new_repl_usb_serial_jtag(
    const esp_console_dev_usb_serial_jtag_config_t *dev_config,
    const esp_console_repl_config_t *repl_config,
    esp_console_repl_t **ret_repl);
esp_err_t esp_console_new_repl_usb_cdc(const esp_console_dev_usb_cdc_config_t *dev_config,
                                       const esp_console_repl_config_t *repl_config,
                                       esp_console_repl_t **ret_repl);

esp_err_t esp_console_start_repl(esp_console_repl_t *repl);

#ifdef __cplusplus
}
#endif
