/*
 * ESP-IDF esp_netif.h stub for desktop simulator
 *
 * Always reports a connected STA netif with the host's primary IP address.
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_netif_t;

/* IP event types */
typedef enum {
    IP_EVENT_STA_GOT_IP,
    IP_EVENT_STA_LOST_IP,
    IP_EVENT_AP_STAIPASSIGNED,
    IP_EVENT_GOT_IP6,
    IP_EVENT_ETH_GOT_IP,
} ip_event_t;

/* IPv4 address info */
typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

/* IPv6 address info */
typedef struct {
    uint32_t addr[4];
    uint8_t zone_id;
} esp_ip6_addr_t;

typedef struct {
    esp_ip6_addr_t ip;
} esp_netif_ip6_info_t;

static inline esp_err_t esp_netif_init(void) { return ESP_OK; }

static inline esp_netif_t esp_netif_create_default_wifi_sta(void) {
    return (esp_netif_t)(uintptr_t)1;
}

static inline esp_netif_t esp_netif_create_default_wifi_ap(void) {
    return (esp_netif_t)(uintptr_t)2;
}

static inline void esp_netif_destroy_default_wifi(void *netif) { (void)netif; }

static inline esp_netif_t esp_netif_get_handle_from_ifkey(const char *if_key) {
    (void)if_key;
    return (esp_netif_t)(uintptr_t)1;
}

static inline esp_err_t esp_netif_get_ip_info(esp_netif_t netif, esp_netif_ip_info_t *ip_info) {
    (void)netif;
    if (!ip_info) return ESP_ERR_INVALID_ARG;
    memset(ip_info, 0, sizeof(*ip_info));
    ip_info->ip.addr = 0x0100007f;       /* 127.0.0.1 (host loopback, agent binds here) */
    ip_info->netmask.addr = 0x00ffffff;   /* 255.255.255.0 */
    ip_info->gw.addr = 0x0100007f;        /* 127.0.0.1 */
    return ESP_OK;
}

static inline esp_err_t esp_netif_get_ip6_linklocal(esp_netif_t netif, esp_netif_ip6_info_t *ip6_info) {
    (void)netif;
    if (ip6_info) memset(ip6_info, 0, sizeof(*ip6_info));
    return ESP_OK;
}

static inline esp_err_t esp_netif_get_ip6_global(esp_netif_t netif, esp_netif_ip6_info_t *ip6_info) {
    (void)netif;
    if (ip6_info) memset(ip6_info, 0, sizeof(*ip6_info));
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
