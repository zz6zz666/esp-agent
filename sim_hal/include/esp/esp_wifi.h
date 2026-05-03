/*
 * esp_wifi.h stub for desktop simulator
 *
 * Always reports WiFi as connected (STA mode). The simulator uses host
 * TCP/IP via libcurl — WiFi/BT control APIs are not exposed to the agent.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
} wifi_mode_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WAPI_PSK,
} wifi_auth_mode_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    wifi_second_chan_t second;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,
    WIFI_SECOND_CHAN_ABOVE,
    WIFI_SECOND_CHAN_BELOW,
} wifi_second_chan_t;

#define WIFI_IF_STA 0
#define WIFI_IF_AP  1
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_BAND_2G 1
#define WIFI_SECONDARY_CHAN_NONE 0

#define WIFI_PSK_MIN_LEN 8
#define WIFI_PSK_MAX_LEN 64

/* ---- Stub implementations: always report connected ---- */

static inline esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    if (mode) *mode = WIFI_MODE_STA;
    return ESP_OK;
}

static inline esp_err_t esp_wifi_get_protocol(uint8_t ifx, uint8_t *proto) {
    (void)ifx;
    if (proto) *proto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
    return ESP_OK;
}

static inline esp_err_t esp_wifi_get_bandwidth(uint8_t ifx, uint8_t *bw) {
    (void)ifx;
    if (bw) *bw = 2; /* HT40 */
    return ESP_OK;
}

static inline esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *info) {
    if (!info) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));
    memcpy(info->ssid, "host-network", 12);
    info->rssi = -45;
    info->authmode = WIFI_AUTH_WPA2_PSK;
    info->primary = 6;
    return ESP_OK;
}

static inline int8_t esp_wifi_sta_get_rssi(void) { return -45; }

static inline esp_err_t esp_wifi_get_channel(uint8_t *primary, uint8_t *secondary) {
    if (primary) *primary = 6;
    if (secondary) *secondary = 0;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
