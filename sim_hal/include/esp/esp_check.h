/*
 * ESP-IDF esp_check.h stub for desktop simulator
 */
#pragma once
#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_FALSE(a, err_code, tag, format, ...) do { \
    if (!(a)) { \
        ESP_LOGE(tag, format, ##__VA_ARGS__); \
        return err_code; \
    } \
} while(0)

#define ESP_RETURN_ON_ERROR(x, tag, format, ...) do { \
    esp_err_t _rc = (x); \
    if (_rc != ESP_OK) { \
        ESP_LOGE(tag, format ": %s", ##__VA_ARGS__, esp_err_to_name(_rc)); \
        return _rc; \
    } \
} while(0)

#define ESP_GOTO_ON_FALSE(a, err_code, goto_tag, tag, format, ...) do { \
    if (!(a)) { \
        ESP_LOGE(tag, format, ##__VA_ARGS__); \
        goto goto_tag; \
    } \
} while(0)

#define ESP_GOTO_ON_ERROR(x, goto_tag, tag, format, ...) do { \
    esp_err_t _rc = (x); \
    if (_rc != ESP_OK) { \
        ESP_LOGE(tag, format ": %s", ##__VA_ARGS__, esp_err_to_name(_rc)); \
        goto goto_tag; \
    } \
} while(0)
