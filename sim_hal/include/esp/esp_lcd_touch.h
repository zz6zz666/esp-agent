/*
 * esp_lcd_touch.h — LCD touch API (simulator implementation backed by SDL2)
 *
 * On real ESP32 these functions talk to I2C/SPI touch controllers.
 * In the simulator, SDL2 mouse events are translated into touch points.
 */

#pragma once
#define _ESP_LCD_TOUCH_HANDLE_T_DEFINED

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t strength;
    uint8_t track_id;
} esp_lcd_touch_point_data_t;

typedef struct {
    gpio_num_t int_gpio_num;
    void *user_data;
} esp_lcd_touch_config_t;

typedef struct esp_lcd_touch_dev_t {
    esp_lcd_touch_config_t config;
    esp_lcd_touch_point_data_t last_point;
    bool has_data;
    uint32_t last_read_ms;
    void (*isr_cb)(void *);
    void *isr_arg;
} esp_lcd_touch_dev_t;

typedef esp_lcd_touch_dev_t *esp_lcd_touch_handle_t;

/* Called by display_sdl2.c to create the global touch device for the emote
 * engine.  Returns a handle that gfx_touch.c will use. */
esp_lcd_touch_handle_t esp_lcd_touch_init_sdl(void);

/* Feed a touch point from SDL mouse events.  Called from the SDL event loop
 * on the main thread (must hold no locks when calling). */
void esp_lcd_touch_feed_sdl(int16_t x, int16_t y, bool pressed);

/* ---- Public API (called by gfx_touch.c) ---- */

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);

esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp,
    esp_lcd_touch_point_data_t *out, uint8_t *count, uint16_t max);

esp_err_t esp_lcd_touch_register_interrupt_callback(
    esp_lcd_touch_handle_t tp, void (*cb)(void *));

esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(
    esp_lcd_touch_handle_t tp, void (*cb)(void *), void *arg);

#ifdef __cplusplus
}
#endif
