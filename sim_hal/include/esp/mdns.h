/*
 * mDNS stub for desktop simulator — all no-ops.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/ip_addr.h"

typedef struct mdns_ip_addr_s {
    struct mdns_ip_addr_s *next;
    ip_addr_t addr;
} mdns_ip_addr_t;

typedef struct {
    const char *key;
    const char *value;
} mdns_txt_item_t;

typedef struct mdns_result_s {
    struct mdns_result_s *next;
    char *hostname;
    char *instance_name;
    uint16_t port;
    mdns_ip_addr_t *addr;
    mdns_txt_item_t *txt;
    size_t txt_count;
} mdns_result_t;

static inline esp_err_t mdns_init(void)                      { return ESP_OK; }
static inline esp_err_t mdns_hostname_set(const char *h)     { (void)h; return ESP_OK; }
static inline esp_err_t mdns_instance_name_set(const char *n){ (void)n; return ESP_OK; }
static inline void     mdns_query_results_free(mdns_result_t *r) { (void)r; }

static inline esp_err_t mdns_query_ptr(const char *s, const char *p,
                                        uint32_t t, int m,
                                        mdns_result_t **r)
{
    (void)s; (void)p; (void)t; (void)m;
    if (r) *r = NULL;
    return ESP_ERR_NOT_FOUND;
}

static inline esp_err_t mdns_service_add(const char *i, const char *s, const char *p,
                                          uint16_t port, mdns_txt_item_t *txt, size_t n)
{
    (void)i; (void)s; (void)p; (void)port; (void)txt; (void)n;
    return ESP_OK;
}

static inline esp_err_t mdns_service_port_set(const char *s, const char *p, uint16_t port)
{ (void)s; (void)p; (void)port; return ESP_OK; }

static inline esp_err_t mdns_service_txt_set(const char *s, const char *p,
                                              mdns_txt_item_t *txt, size_t n)
{ (void)s; (void)p; (void)txt; (void)n; return ESP_OK; }

static inline esp_err_t mdns_service_remove(const char *s, const char *p)
{ (void)s; (void)p; return ESP_OK; }

#ifdef __cplusplus
}
#endif
