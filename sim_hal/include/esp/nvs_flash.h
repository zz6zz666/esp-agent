/*
 * ESP-IDF nvs_flash.h stub for desktop simulator
 *
 * NVS (Non-Volatile Storage) maps to a JSON file on desktop.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

#ifdef __cplusplus
}
#endif
