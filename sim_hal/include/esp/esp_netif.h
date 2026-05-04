/*
 * ESP-IDF esp_netif.h stub for desktop simulator
 *
 * Always reports a connected STA netif with the host's primary IP address.
 */
#pragma once
#include "esp_err.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <net/if.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_netif_t;

/* IPSTR / IP2STR macros for printf-style formatting of IPv4 addresses */
#define IPSTR           "%d.%d.%d.%d"
#define IP2STR(ipaddr)  (int)((ipaddr)->addr & 0xff), \
                        (int)(((ipaddr)->addr >> 8) & 0xff), \
                        (int)(((ipaddr)->addr >> 16) & 0xff), \
                        (int)(((ipaddr)->addr >> 24) & 0xff)

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

    struct ifaddrs *ifaddr = NULL, *ifa;
    uint32_t found_ip = 0, found_mask = 0;

    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            /* Skip loopback — pick the first real interface */
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            /* s_addr on LE x86 already matches ESP32 convention (first octet in LSB).
             * ntohl would reverse the bytes, so we use the raw s_addr value directly. */
            found_ip = sin->sin_addr.s_addr;
            if (ifa->ifa_netmask) {
                struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;
                found_mask = nm->sin_addr.s_addr;
            }
            break;
        }
        freeifaddrs(ifaddr);
    }

    memset(ip_info, 0, sizeof(*ip_info));
    if (found_ip) {
        ip_info->ip.addr = found_ip;
        ip_info->netmask.addr = found_mask ? found_mask : 0x00ffffff;
        /* Gateway: common convention is .1 on the same subnet */
        ip_info->gw.addr = (found_ip & found_mask) | 0x01000000;
    } else {
        /* Fallback: loopback */
        ip_info->ip.addr = 0x0100007f;
        ip_info->netmask.addr = 0x00ffffff;
        ip_info->gw.addr = 0x0100007f;
    }
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
