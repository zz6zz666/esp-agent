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

/* ---- Platform-specific WiFi helpers ---- */

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <wlanapi.h>
# include <stdlib.h>
# pragma comment(lib, "wlanapi.lib")

/* Missing defines for newer auth algorithms (safe for older SDKs) */
#ifndef DOT11_AUTH_ALGO_WPA3
# define DOT11_AUTH_ALGO_WPA3     ((DOT11_AUTH_ALGORITHM)8)
#endif
#ifndef DOT11_AUTH_ALGO_WPA3_ENT
# define DOT11_AUTH_ALGO_WPA3_ENT ((DOT11_AUTH_ALGORITHM)9)
#endif
#ifndef DOT11_AUTH_ALGO_OWE
# define DOT11_AUTH_ALGO_OWE      ((DOT11_AUTH_ALGORITHM)10)
#endif

/* Frequency (MHz) to 802.11 channel number */
static inline uint8_t _wifi_freq_to_channel(ULONG freqMHz) {
    if (freqMHz == 2484) return 14;
    if (freqMHz >= 2412 && freqMHz <= 2472)
        return (uint8_t)((freqMHz - 2412) / 5 + 1);
    if (freqMHz >= 5180 && freqMHz <= 5885)
        return (uint8_t)((freqMHz - 5000) / 5);
    return 0;
}

/* Map DOT11_AUTH_ALGORITHM to wifi_auth_mode_t.
   Use int for switch to handle values beyond the SDK's enum. */
static inline wifi_auth_mode_t _wifi_auth_to_mode(DOT11_AUTH_ALGORITHM algo) {
    switch ((int)algo) {
    case DOT11_AUTH_ALGO_80211_OPEN:       return WIFI_AUTH_OPEN;
    case DOT11_AUTH_ALGO_80211_SHARED_KEY: return WIFI_AUTH_WEP;
    case DOT11_AUTH_ALGO_WPA:              return WIFI_AUTH_WPA_PSK;
    case DOT11_AUTH_ALGO_WPA_PSK:          return WIFI_AUTH_WPA_PSK;
    case DOT11_AUTH_ALGO_WPA_NONE:         return WIFI_AUTH_WPA_PSK;
    case DOT11_AUTH_ALGO_RSNA:             return WIFI_AUTH_WPA2_PSK;
    case DOT11_AUTH_ALGO_RSNA_PSK:         return WIFI_AUTH_WPA2_PSK;
    case 8:  return WIFI_AUTH_WPA3_PSK;       /* DOT11_AUTH_ALGO_WPA3 */
    case 9:  return WIFI_AUTH_WPA2_ENTERPRISE; /* DOT11_AUTH_ALGO_WPA3_ENT */
    case 10: return WIFI_AUTH_WPA3_PSK;        /* DOT11_AUTH_ALGO_OWE */
    default: return WIFI_AUTH_OPEN;
    }
}

/* Channel cache shared by _wifi_fill_ap_info and esp_wifi_get_channel */
static struct { int valid; uint8_t primary; wifi_second_chan_t second; } _wifi_chan_cache;

/* WLAN API wrapper */
static inline HANDLE _wifi_wlan_open(void) {
    DWORD negotiatedVersion;
    HANDLE hClient = NULL;
    DWORD ret = WlanOpenHandle(2, NULL, &negotiatedVersion, &hClient);
    if (ret == ERROR_SERVICE_NOT_ACTIVE || ret != ERROR_SUCCESS) {
        ret = WlanOpenHandle(1, NULL, &negotiatedVersion, &hClient);
    }
    return (ret == ERROR_SUCCESS) ? hClient : NULL;
}

/* Helper: find first wireless interface and fill all AP info fields */
static inline int _wifi_fill_ap_info(wifi_ap_record_t *info)
{
    memset(info, 0, sizeof(*info));
    _wifi_chan_cache.valid = 0;

    HANDLE hClient = _wifi_wlan_open();
    if (!hClient) return 0;

    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    if (WlanEnumInterfaces(hClient, NULL, &pIfList) != ERROR_SUCCESS) {
        WlanCloseHandle(hClient, NULL);
        return 0;
    }

    int found = 0;
    for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
        PWLAN_INTERFACE_INFO pIfInfo = &pIfList->InterfaceInfo[i];
        if (pIfInfo->isState != wlan_interface_state_connected) continue;

        /* --- Primary path: BSS list (best accuracy) --- */
        PWLAN_BSS_LIST pBssList = NULL;
        if (WlanGetNetworkBssList(hClient, &pIfInfo->InterfaceGuid,
                NULL, dot11_BSS_type_infrastructure, TRUE, NULL,
                &pBssList) == ERROR_SUCCESS && pBssList->dwNumberOfItems > 0) {
            WLAN_BSS_ENTRY *bss = &pBssList->wlanBssEntries[0];

            info->rssi = bss->lRssi;

            size_t ssid_len = bss->dot11Ssid.uSSIDLength;
            if (ssid_len > 32) ssid_len = 32;
            memcpy(info->ssid, bss->dot11Ssid.ucSSID, ssid_len);
            info->ssid[ssid_len] = '\0';

            memcpy(info->bssid, bss->dot11Bssid, 6);

            info->primary = _wifi_freq_to_channel(bss->ulChCenterFrequency);
            _wifi_chan_cache.primary = info->primary;
            _wifi_chan_cache.second = WIFI_SECOND_CHAN_NONE;
            _wifi_chan_cache.valid = 1;

            found = 1;
            WlanFreeMemory(pBssList);
        }

        /* --- Always query connection attributes for auth mode --- */
        {
            PWLAN_CONNECTION_ATTRIBUTES pConnAttr = NULL;
            DWORD attrSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
            if (WlanQueryInterface(hClient, &pIfInfo->InterfaceGuid,
                    wlan_intf_opcode_current_connection, NULL,
                    &attrSize, (PVOID *)&pConnAttr, NULL) == ERROR_SUCCESS) {
                if (pConnAttr && pConnAttr->isState == wlan_interface_state_connected) {
                    info->authmode = _wifi_auth_to_mode(
                        pConnAttr->wlanSecurityAttributes.dot11AuthAlgorithm);

                    /* Fallback: BSS list failed — get everything from assoc */
                    if (!found) {
                        PWLAN_ASSOCIATION_ATTRIBUTES assoc = &pConnAttr->wlanAssociationAttributes;
                        size_t ssid_len = assoc->dot11Ssid.uSSIDLength;
                        if (ssid_len > 32) ssid_len = 32;
                        memcpy(info->ssid, assoc->dot11Ssid.ucSSID, ssid_len);
                        info->ssid[ssid_len] = '\0';
                        info->rssi = (int8_t)(assoc->wlanSignalQuality / 2 - 100);
                        memcpy(info->bssid, assoc->dot11Bssid, 6);
                        found = 1;
                    }
                }
                WlanFreeMemory(pConnAttr);
            }
        }

        if (found) break;
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
    return found;
}

#else
/* Helper: find first wireless interface name.  Returns 1 on success. */
static inline int _wifi_find_iface(char *ifname, size_t size)
{
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ' ' || line[0] == 'I' || line[0] == '\n')
            continue;
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
        if (n > 0 && ssid[n - 1] == '\n') ssid[--n] = '\0';
        return 1;
    }
    return 0;
}
#endif

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

#if defined(PLATFORM_WINDOWS)
    if (_wifi_fill_ap_info(info))
        return ESP_OK;
    return ESP_ERR_NOT_FOUND;
#else
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
                    char *q = strstr(line, "link");
                    if (q) {
                        int link, level, noise;
                        if (sscanf(q, "link=%d level=%d noise=%d", &link, &level, &noise) == 3
                            || sscanf(q, "link=%d level=%d", &link, &level) == 2) {
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
#endif
}

static inline int8_t esp_wifi_sta_get_rssi(void) {
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK)
        return info.rssi;
    return 0;
}

static inline esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary) {
#if defined(PLATFORM_WINDOWS)
    if (_wifi_chan_cache.valid) {
        if (primary) *primary = _wifi_chan_cache.primary;
        if (secondary) *secondary = _wifi_chan_cache.second;
    } else {
        if (primary) *primary = 1;
        if (secondary) *secondary = WIFI_SECOND_CHAN_NONE;
    }
    return ESP_OK;
#else
    if (primary) *primary = 1;
    if (secondary) *secondary = WIFI_SECOND_CHAN_NONE;
    return ESP_OK;
#endif
}

#ifdef __cplusplus
}
#endif
