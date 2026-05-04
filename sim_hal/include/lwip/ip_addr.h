/*
 * lwIP IP address stub for desktop simulator.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int addr;
} ip_addr_t;

static inline char *ipaddr_ntoa_r(const ip_addr_t *addr, char *buf, size_t len)
{
    (void)addr;
    (void)len;
    if (buf) buf[0] = '\0';
    return buf;
}

#ifdef __cplusplus
}
#endif
