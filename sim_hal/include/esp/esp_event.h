/*
 * ESP-IDF esp_event.h stub for desktop simulator
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_event_loop_handle_t;
typedef const char *esp_event_base_t;

typedef void (*esp_event_handler_t)(void *event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data);

/* Minimal stub: event loop creation always succeeds */
static inline esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }

#ifdef __cplusplus
}
#endif
