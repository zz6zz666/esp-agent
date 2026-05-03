/*
 * esp_sntp.h stub for desktop simulator.
 * SNTP time sync is replaced by system time on desktop.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*sync_cb)(struct timeval *tv);
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) { .sync_cb = NULL }

static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) {
    (void)config;
    return ESP_OK;
}
static inline esp_err_t esp_netif_sntp_deinit(void) { return ESP_OK; }
static inline esp_err_t esp_netif_sntp_sync_wait(uint32_t timeout_ms) {
    (void)timeout_ms;
    return ESP_OK; /* Pretend sync succeeded */
}

#ifdef __cplusplus
}
#endif
