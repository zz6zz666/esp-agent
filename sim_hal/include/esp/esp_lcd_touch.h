/*
 * esp_lcd_touch.h — LCD touch API stub for desktop simulator
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

static inline esp_lcd_touch_handle_t esp_lcd_touch_init_sdl(void)
    { return NULL; }

static inline void esp_lcd_touch_feed_sdl(int16_t x, int16_t y, bool pressed)
    { (void)x; (void)y; (void)pressed; }

static inline esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp)
    { (void)tp; return ESP_OK; }

static inline esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp,
    esp_lcd_touch_point_data_t *out, uint8_t *count, uint16_t max)
    { (void)tp; (void)out; (void)max; if (count) *count = 0; return ESP_OK; }

static inline esp_err_t esp_lcd_touch_register_interrupt_callback(
    esp_lcd_touch_handle_t tp, void (*cb)(esp_lcd_touch_handle_t))
    { (void)tp; (void)cb; return ESP_OK; }

static inline esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(
    esp_lcd_touch_handle_t tp, void (*cb)(esp_lcd_touch_handle_t), void *arg)
    { (void)tp; (void)cb; (void)arg; return ESP_OK; }

#ifdef __cplusplus
}
#endif
