/*
 * HTTP server stub for desktop simulator.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t server_port;
    uint16_t ctrl_port;
    uint16_t max_uri_handlers;
    uint32_t stack_size;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() \
    ((httpd_config_t){ .server_port = 80, .ctrl_port = 32768, .max_uri_handlers = 8, .stack_size = 4096 })

#ifdef __cplusplus
}
#endif
