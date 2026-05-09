/*
 * esp_websocket_client_stub.c — Android stub for esp_websocket_client
 *
 * All functions return ESP_FAIL or equivalent.
 */
#include "esp_websocket_client.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ws_stub";

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config)
{
    (void)config;
    ESP_LOGW(TAG, "esp_websocket_client_init: stub");
    return NULL;
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client)
{
    (void)client;
    return ESP_FAIL;
}

esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
                                        esp_websocket_event_id_t event,
                                        esp_event_handler_t cb, void *arg)
{
    (void)client; (void)event; (void)cb; (void)arg;
    return ESP_OK;
}

int esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                   const char *data, int len, int timeout_ms)
{
    (void)client; (void)data; (void)len; (void)timeout_ms;
    return 0;
}

int esp_websocket_client_send_bin(esp_websocket_client_handle_t client,
                                  const char *data, int len, int timeout_ms)
{
    (void)client; (void)data; (void)len; (void)timeout_ms;
    return 0;
}

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client)
{
    (void)client;
    return false;
}
