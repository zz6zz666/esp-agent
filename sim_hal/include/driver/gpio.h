/* Stub for driver/gpio.h */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

typedef int gpio_num_t;

#define GPIO_NUM_NC    (-1)
#define GPIO_IS_VALID_GPIO(gpio_num)  ((gpio_num) >= 0)

static inline esp_err_t gpio_set_level(gpio_num_t gpio, int level) { (void)gpio; (void)level; return ESP_OK; }
static inline esp_err_t gpio_set_direction(gpio_num_t gpio, int mode) { (void)gpio; (void)mode; return ESP_OK; }
static inline esp_err_t gpio_intr_disable(gpio_num_t gpio) { (void)gpio; return ESP_OK; }
