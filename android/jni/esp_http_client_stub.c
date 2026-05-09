/*
 * esp_http_client_stub.c — Android stub for esp_http_client
 *
 * All functions return ESP_FAIL (not connected to network).
 * On Android, HTTP requests go through the JNI-based HttpTransport instead.
 */
#include "esp_http_client.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_stub";

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    (void)config;
    ESP_LOGW(TAG, "esp_http_client_init: stub (use JNI instead)");
    return NULL;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method)
{
    (void)client; (void)method;
    return ESP_FAIL;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    (void)client; (void)key; (void)value;
    return ESP_FAIL;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len)
{
    (void)client; (void)data; (void)len;
    return ESP_FAIL;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    (void)client;
    return 0;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}

/* Streaming API */
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len)
{
    (void)client; (void)write_len;
    return ESP_FAIL;
}

int esp_http_client_write(esp_http_client_handle_t client, const char *data, int len)
{
    (void)client; (void)data;
    return len; /* pretend all was written */
}

int esp_http_client_read(esp_http_client_handle_t client, char *buf, int len)
{
    (void)client; (void)buf; (void)len;
    return 0;
}

int esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    (void)client;
    return 0;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}
