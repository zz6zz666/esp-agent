/*
 * esp_wifi.h stub for desktop simulator
 *
 * Checks /proc/net/wireless for real WiFi interfaces.  When none are found
 * (e.g. WSL2, wired-only machines) ESP_ERR_NOT_FOUND is returned, which
 * cap_system handles gracefully by reporting "disconnected".
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>
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

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,
    WIFI_SECOND_CHAN_ABOVE,
    WIFI_SECOND_CHAN_BELOW,
} wifi_second_chan_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    wifi_second_chan_t second;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

#define WIFI_IF_STA 0
#define WIFI_IF_AP  1
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_BAND_2G 1
#define WIFI_SECONDARY_CHAN_NONE 0

#define WIFI_PSK_MIN_LEN 8
#define WIFI_PSK_MAX_LEN 64

/* Helper: find first wireless interface name.  Returns 1 on success. */
static inline int _wifi_find_iface(char *ifname, size_t size)
{
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    /* Skip header lines */
    while (fgets(line, sizeof(line), f)) {
        /* Look for a line starting with an interface name (not "Inter-" or " face") */
        if (line[0] == ' ' || line[0] == 'I' || line[0] == '\n')
            continue;
        /* Lines have format: ifname: ... */
        char *colon = strchr(line, ':');
        if (colon) {
            size_t len = (size_t)(colon - line);
            if (len >= size) len = size - 1;
            memcpy(ifname, line, len);
            ifname[len] = '\0';
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Helper: try to read SSID from /sys/class/net/<iface>/wireless/ssid */
static inline int _wifi_read_ssid(const char *ifname, char *ssid, size_t size)
{
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless/ssid", ifname);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(ssid, 1, size - 1, f);
    fclose(f);
    if (n > 0) {
        ssid[n] = '\0';
        /* Trim trailing newline */
        if (n > 0 && ssid[n - 1] == '\n') ssid[--n] = '\0';
        return 1;
    }
    return 0;
}

/* ---- Stub implementations ---- */

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

    char ifname[32] = {0};
    if (!_wifi_find_iface(ifname, sizeof(ifname)))
        return ESP_ERR_NOT_FOUND;

    memset(info, 0, sizeof(*info));

    /* SSID from sysfs if available */
    char ssid[33] = {0};
    if (!_wifi_read_ssid(ifname, ssid, sizeof(ssid)))
        snprintf(ssid, sizeof(ssid), "%s", ifname);

    memcpy(info->ssid, ssid, strlen(ssid) < 33 ? strlen(ssid) : 32);

    /* RSSI from /proc/net/wireless (link quality) */
    {
        FILE *f = fopen("/proc/net/wireless", "r");
        if (f) {
            char line[256];
            int rssi_raw = -45;
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, ifname, strlen(ifname)) == 0) {
                    /* Format: ifname: status quality(link|level|noise) ... */
                    char *q = strstr(line, "link");
                    if (q) {
                        int link, level, noise;
                        if (sscanf(q, "link=%d level=%d noise=%d", &link, &level, &noise) == 3
                            || sscanf(q, "link=%d level=%d", &link, &level) == 2) {
                            /* level is typically negative dBm */
                            rssi_raw = level;
                        }
                    }
                    break;
                }
            }
            fclose(f);
            info->rssi = (int8_t)rssi_raw;
        }
    }

    info->authmode = WIFI_AUTH_WPA2_PSK;
    info->primary = 1;
    return ESP_OK;
}

static inline int8_t esp_wifi_sta_get_rssi(void) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK)
        return info.rssi;
    return 0;
}

static inline esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary) {
    if (primary) *primary = 1;
    if (secondary) *secondary = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
