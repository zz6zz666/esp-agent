/*
 * esp_board_manager_stub.c — simulator implementations
 *
 * Returns valid simulator data for display_lcd, dummy errors for everything else.
 */
#include "esp_board_manager_includes.h"
#include <string.h>

/* Static simulated display handles */
static void *s_panel_handle = (void *)0x1;  /* non-NULL sentinel */
static void *s_io_handle    = (void *)0x2;

static dev_display_lcd_handles_t s_lcd_handles = {
    .panel_handle = NULL,   /* init below */
    .io_handle    = NULL,
};

static dev_display_lcd_config_t s_lcd_config = {
    .lcd_width  = 480,
    .lcd_height = 480,
    .sub_type   = "io",
};

static esp_board_info_t s_board_info = {
    .name         = "esp-claw-sim",
    .chip         = "linux-x86_64",
    .version      = "1.0.0",
    .description  = "Desktop simulator for esp-claw",
    .manufacturer = "esp-claw",
};

esp_err_t esp_board_manager_get_board_info(esp_board_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    memcpy(info, &s_board_info, sizeof(s_board_info));
    return ESP_OK;
}

esp_err_t esp_board_manager_get_device_handle(const char *name, void **handle)
{
    if (!name || !handle) return ESP_ERR_INVALID_ARG;

    if (strcmp(name, ESP_BOARD_DEVICE_NAME_DISPLAY_LCD) == 0) {
        s_lcd_handles.panel_handle = s_panel_handle;
        s_lcd_handles.io_handle    = s_io_handle;
        *handle = &s_lcd_handles;
        return ESP_OK;
    }

    *handle = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_board_manager_get_device_config(const char *name, void **config)
{
    if (!name || !config) return ESP_ERR_INVALID_ARG;

    if (strcmp(name, ESP_BOARD_DEVICE_NAME_DISPLAY_LCD) == 0) {
        *config = &s_lcd_config;
        return ESP_OK;
    }

    *config = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_board_manager_get_periph_config(const char *name, void **config)
{
    (void)name;
    if (config) *config = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_board_manager_init_device_by_name(const char *name)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    if (strcmp(name, ESP_BOARD_DEVICE_NAME_DISPLAY_LCD) == 0)
        return ESP_OK;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_board_manager_deinit_device_by_name(const char *name)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    if (strcmp(name, ESP_BOARD_DEVICE_NAME_DISPLAY_LCD) == 0)
        return ESP_OK;
    return ESP_ERR_NOT_SUPPORTED;
}
