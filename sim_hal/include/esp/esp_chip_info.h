/*
 * ESP-IDF esp_chip_info.h stub for desktop simulator
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t model;
    uint8_t cores;
    uint8_t revision;
} esp_chip_info_t;

static inline void esp_chip_info(esp_chip_info_t *info) {
    static esp_chip_info_t fake = { .model = 0, .cores = 8, .revision = 0 };
    if (info) *info = fake;
}

#ifdef __cplusplus
}
#endif
