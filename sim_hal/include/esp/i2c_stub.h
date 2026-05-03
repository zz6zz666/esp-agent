/*
 * I2C stub for desktop simulator — all functions return ESP_OK.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int i2c_port_t;
typedef void *i2c_bus_handle_t;
typedef void *i2c_dev_handle_t;

#define I2C_NUM_0 0
#define I2C_NUM_1 1

static inline esp_err_t i2c_master_init(i2c_port_t port, int sda, int scl, uint32_t freq) {
    (void)port; (void)sda; (void)scl; (void)freq; return ESP_OK;
}

#ifdef __cplusplus
}
#endif
