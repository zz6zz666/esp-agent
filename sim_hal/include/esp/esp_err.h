/*
 * ESP-IDF esp_err.h stub for desktop simulator
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

#define ESP_OK                   0
#define ESP_FAIL                 -1
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_INVALID_SIZE     0x104
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_NOT_SUPPORTED    0x106
#define ESP_ERR_TIMEOUT          0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_HTTP_CONNECT    0x7002
#define ESP_ERR_HTTP_CONNECTING 0x7003
#define ESP_ERR_NVS_NO_FREE_PAGES        0x1101
#define ESP_ERR_NVS_NEW_VERSION_FOUND    0x1102

#define ESP_ERROR_CHECK(x) do {                                         \
        esp_err_t __err_rc = (x);                                       \
        if (__err_rc != ESP_OK) {                                       \
            fprintf(stderr, "ESP_ERROR_CHECK failed: %s:%d (%s): %d\n", \
                    __FILE__, __LINE__, __func__, (int)__err_rc);       \
            abort();                                                    \
        }                                                               \
    } while(0)

static inline const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:                  return "ESP_OK";
    case ESP_FAIL:                return "ESP_FAIL";
    case ESP_ERR_NO_MEM:          return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:     return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:   return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:    return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:       return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:   return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:         return "ESP_ERR_TIMEOUT";
    case ESP_ERR_HTTP_CONNECT:    return "ESP_ERR_HTTP_CONNECT";
    default:                      return "ESP_ERR_UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif
