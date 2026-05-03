/*
 * GPIO stub for desktop simulator — all functions return ESP_OK.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common GPIO types */
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_INPUT_OUTPUT,
} gpio_mode_t;

static inline esp_err_t gpio_set_direction(gpio_num_t n, gpio_mode_t m) { (void)n; (void)m; return ESP_OK; }
static inline esp_err_t gpio_set_level(gpio_num_t n, uint32_t l) { (void)n; (void)l; return ESP_OK; }
static inline int gpio_get_level(gpio_num_t n) { (void)n; return 0; }
static inline esp_err_t gpio_reset_pin(gpio_num_t n) { (void)n; return ESP_OK; }

#ifdef __cplusplus
}
#endif
