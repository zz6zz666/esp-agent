/*
 * esp_http_client.h — POSIX stub for desktop simulator
 *
 * Replaces ESP-IDF's esp_http_client with a libcurl-based
 * implementation.  Types and API match the subset used by
 * cap_im_qq / cap_im_tg / cap_im_feishu / cap_im_wechat /
 * cap_im_attachment.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_http_client_handle_t;

/* HTTP methods */
typedef enum {
    HTTP_METHOD_GET     = 0,
    HTTP_METHOD_POST    = 1,
    HTTP_METHOD_PUT     = 2,
    HTTP_METHOD_DELETE  = 3,
    HTTP_METHOD_HEAD    = 4,
    HTTP_METHOD_PATCH   = 5,
    HTTP_METHOD_NOTIFY  = 6,
    HTTP_METHOD_SUBSCRIBE = 7,
    HTTP_METHOD_UNSUBSCRIBE = 8,
    HTTP_METHOD_OPTIONS = 9,
} esp_http_client_method_t;

/* Auth types */
typedef enum {
    HTTP_AUTH_TYPE_NONE   = 0,
    HTTP_AUTH_TYPE_BASIC  = 1,
    HTTP_AUTH_TYPE_DIGEST = 2,
} esp_http_client_auth_type_t;

/* Transport type */
typedef enum {
    HTTP_TRANSPORT_UNKNOWN = 0,
    HTTP_TRANSPORT_OVER_TCP,
    HTTP_TRANSPORT_OVER_SSL,
} esp_http_client_transport_t;

/* Event IDs */
typedef enum {
    HTTP_EVENT_ERROR           = 0,
    HTTP_EVENT_ON_CONNECTED    = 1,
    HTTP_EVENT_HEADERS_SENT    = 2,
    HTTP_EVENT_HEADER_SENT     = 2,
    HTTP_EVENT_ON_HEADER       = 3,
    HTTP_EVENT_ON_DATA         = 4,
    HTTP_EVENT_ON_FINISH       = 5,
    HTTP_EVENT_DISCONNECTED    = 6,
    HTTP_EVENT_REDIRECT        = 7,
} esp_http_client_event_id_t;

typedef struct {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t   client;
    void                      *user_data;
    char                      *data;
    int                        data_len;
    void                      *header_key;
    void                      *header_value;
    int                        status_code;
} esp_http_client_event_t;

typedef esp_err_t (*esp_http_client_event_handler_t)(esp_http_client_event_t *evt);

typedef struct {
    const char                    *url;
    const char                    *host;
    int                            port;
    const char                    *path;
    const char                    *query;
    const char                    *cert_pem;
    const char                    *client_cert_pem;
    const char                    *client_key_pem;
    const char                    *username;
    const char                    *password;
    esp_http_client_auth_type_t    auth_type;
    esp_http_client_method_t       method;
    int                            timeout_ms;
    bool                           disable_auto_redirect;
    int                            max_redirection_count;
    int                            max_authorization_retries;
    esp_http_client_event_handler_t event_handler;
    void                          *user_data;
    int                            buffer_size;
    int                            buffer_size_tx;
    bool                           skip_cert_common_name_check;
    bool                           keep_alive_enable;
    int                            keep_alive_idle;
    int                            keep_alive_interval;
    int                            keep_alive_count;
    int                            transport_type;
    bool                           is_async;
    void                         (*crt_bundle_attach)(void *conf);
    const char                    *common_name;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int      esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);

/* Streaming API — used by cap_im_tg and cap_im_feishu */
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int      esp_http_client_write(esp_http_client_handle_t client, const char *data, int len);
int      esp_http_client_read(esp_http_client_handle_t client, char *buf, int len);
int      esp_http_client_fetch_headers(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);

#ifdef __cplusplus
}
#endif
