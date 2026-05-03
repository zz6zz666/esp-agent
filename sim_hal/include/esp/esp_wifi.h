/*
 * esp_wifi.h stub for desktop simulator
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
} wifi_mode_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    int8_t rssi;
    wifi_mode_t mode;
    bool connected;
} wifi_ap_record_t;

#define WIFI_IF_STA 0
#define WIFI_IF_AP  1
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_BAND_2G 1
#define WIFI_SECONDARY_CHAN_NONE 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_CIPHER_TYPE_CCMP 3

#define WIFI_PSK_MIN_LEN 8
#define WIFI_PSK_MAX_LEN 64

static inline esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
    if (mode) *mode = WIFI_MODE_NULL; return ESP_OK;
}
static inline esp_err_t esp_wifi_get_protocol(uint8_t ifx, uint8_t *proto) {
    (void)ifx; if (proto) *proto = 0; return ESP_OK;
}
static inline esp_err_t esp_wifi_get_bandwidth(uint8_t ifx, uint8_t *bw) {
    (void)ifx; if (bw) *bw = 0; return ESP_OK;
}
static inline esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *info) {
    memset(info, 0, sizeof(*info)); return ESP_OK;
}
static inline int8_t esp_wifi_sta_get_rssi(void) { return -50; }
static inline esp_err_t esp_wifi_get_channel(uint8_t *primary, uint8_t *secondary) {
    if (primary) *primary = 1; if (secondary) *secondary = 0; return ESP_OK;
}

#ifdef __cplusplus
}
#endif
