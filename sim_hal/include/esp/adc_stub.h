/*
 * ADC stub for desktop simulator — all functions return ESP_OK.
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t adc1_config_channel_atten(int ch, int atten) {
    (void)ch; (void)atten; return ESP_OK;
}
static inline int adc1_get_raw(int ch) { (void)ch; return 0; }

#ifdef __cplusplus
}
#endif
