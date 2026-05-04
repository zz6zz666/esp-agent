/*
 * ESP-IDF esp_netif.h stub for desktop simulator
 *
 * Reports the host's primary IP address.
 * On Windows: GetAdaptersAddresses (iphlpapi)
 * On POSIX:   getifaddrs()
 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <iphlpapi.h>
# pragma comment(lib, "iphlpapi.lib")
#else
# include <arpa/inet.h>
# include <ifaddrs.h>
# include <sys/socket.h>
# include <net/if.h>
#endif

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

#if defined(PLATFORM_WINDOWS)
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t netif, esp_netif_ip_info_t *ip_info) {
    (void)netif;
    if (!ip_info) return ESP_ERR_INVALID_ARG;

    memset(ip_info, 0, sizeof(*ip_info));
    uint32_t found_ip = 0, found_mask = 0;

    ULONG bufSize = 15000;
    PIP_ADAPTER_ADDRESSES adapters = (PIP_ADAPTER_ADDRESSES)malloc(bufSize);
    if (!adapters) goto fallback;

    ULONG ret = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            NULL, adapters, &bufSize);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        free(adapters);
        adapters = (PIP_ADAPTER_ADDRESSES)malloc(bufSize);
        if (!adapters) goto fallback;
        ret = GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                NULL, adapters, &bufSize);
    }

    if (ret == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES a = adapters; a; a = a->Next) {
            /* Skip loopback and tunnel adapters */
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (a->OperStatus != IfOperStatusUp) continue;
            for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
                if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                    struct sockaddr_in *sin = (struct sockaddr_in *)ua->Address.lpSockaddr;
                    found_ip = (uint32_t)sin->sin_addr.S_un.S_addr;
                    /* Netmask from prefix length */
                    uint8_t prefix = ua->OnLinkPrefixLength;
                    found_mask = prefix ? (0xFFFFFFFF << (32 - prefix)) & 0xFFFFFFFF : 0x00FFFFFF;
                    goto done;
                }
            }
        }
    }
done:
    if (adapters) free(adapters);

    if (found_ip) {
        ip_info->ip.addr = found_ip;
        ip_info->netmask.addr = found_mask ? found_mask : 0x00ffffff;
        ip_info->gw.addr = (found_ip & found_mask) | 0x01000000;
    } else {
fallback:
        ip_info->ip.addr = 0x0100007f;
        ip_info->netmask.addr = 0x00ffffff;
        ip_info->gw.addr = 0x0100007f;
    }
    return ESP_OK;
}
#else
static inline esp_err_t esp_netif_get_ip_info(esp_netif_t netif, esp_netif_ip_info_t *ip_info) {
    (void)netif;
    if (!ip_info) return ESP_ERR_INVALID_ARG;

    struct ifaddrs *ifaddr = NULL, *ifa;
    uint32_t found_ip = 0, found_mask = 0;

    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
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
        ip_info->gw.addr = (found_ip & found_mask) | 0x01000000;
    } else {
        ip_info->ip.addr = 0x0100007f;
        ip_info->netmask.addr = 0x00ffffff;
        ip_info->gw.addr = 0x0100007f;
    }
    return ESP_OK;
}
#endif

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
