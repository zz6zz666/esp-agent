/*
 * ESP-IDF esp_netif_sntp.h stub for desktop simulator
 *
 * On Linux the system clock is already NTP-synced, so cap_time_is_valid()
 * returns true immediately and the SNTP code path is never reached.
 * These stubs exist only to satisfy the compiler.
 */
#pragma once

#include <sys/time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_SNTP_SERVER_LIST(...)  __VA_ARGS__

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server)            \
    ((esp_sntp_config_t) {                               \
        .server_name     = (server),                     \
        .server_count    = 1,                            \
        .servers         = { (server) },                 \
        .start           = false,                        \
        .wait_for_sync   = true,                         \
        .sync_cb         = NULL,                         \
        .renew_servers_after_new_IP = false,             \
        .index_of_first_server = 0,                      \
        .ip_event_to_renew = 0,                          \
    })

#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(num, ...) \
    ((esp_sntp_config_t) {                               \
        .server_count    = (num),                        \
        .servers         = { __VA_ARGS__ },              \
        .start           = false,                        \
        .wait_for_sync   = true,                         \
        .sync_cb         = NULL,                         \
        .renew_servers_after_new_IP = false,             \
        .index_of_first_server = 0,                      \
        .ip_event_to_renew = 0,                          \
    })

typedef struct {
    const char *server_name;
    const char *servers[16];
    size_t server_count;
    bool start;
    bool wait_for_sync;
    bool renew_servers_after_new_IP;
    size_t index_of_first_server;
    int ip_event_to_renew;
    void (*sync_cb)(struct timeval *tv);
} esp_sntp_config_t;

static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config)
{
    (void)config;
    return ESP_OK;
}

static inline esp_err_t esp_netif_sntp_sync_wait(TickType_t timeout_ms)
{
    (void)timeout_ms;
    /* On Linux the system clock is always valid, so we never actually
     * wait for SNTP — this should not be called, but if it is, succeed. */
    return ESP_OK;
}

static inline void esp_netif_sntp_deinit(void) {}

#ifdef __cplusplus
}
#endif
