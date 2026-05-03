/*
 * esp_lcd_panel_stub.c — Bridges ESP-IDF LCD panel API to display_hal.
 *
 * emote_flush_callback() calls esp_lcd_panel_draw_bitmap() to push rendered
 * frames to the LCD.  On the simulator this goes to the SDL2 back-buffer.
 */
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "display_hal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lcd_panel_stub";

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,
                                    int x_start, int y_start,
                                    int x_end, int y_end,
                                    const void *color_data)
{
    (void)panel;

    if (!color_data) return ESP_ERR_INVALID_ARG;

    /* gfx passes exclusive x_end/y_end (last-pixel + 1).  ESP-IDF's
       esp_lcd_panel_draw_bitmap takes inclusive coords on hardware,
       but on the simulator we convert to (x, y, w, h). */
    int w = x_end - x_start;
    int h = y_end - y_start;

    if (w <= 0 || h <= 0) return ESP_OK;

    return display_hal_draw_bitmap(x_start, y_start, w, h,
                                   (const uint16_t *)color_data);
}

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel)
{
    (void)panel;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel)
{
    (void)panel;
    ESP_LOGI(TAG, "LCD panel init (simulated)");
    return ESP_OK;
}

esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on_off)
{
    (void)panel; (void)on_off;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t panel, bool mx, bool my)
{
    (void)panel; (void)mx; (void)my;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t panel, bool swap)
{
    (void)panel; (void)swap;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t panel, bool invert)
{
    (void)panel; (void)invert;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd,
                                     const void *param, size_t size)
{
    (void)io; (void)cmd; (void)param; (void)size;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd,
                                     const void *color, size_t size)
{
    (void)io; (void)cmd; (void)color; (void)size;
    return ESP_OK;
}
