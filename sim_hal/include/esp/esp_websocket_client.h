/*
 * esp_websocket_client.h — POSIX stub for desktop simulator
 *
 * Replaces ESP-IDF's esp_websocket_client with a libcurl-based
 * implementation.  The API surface matches the subset used by
 * cap_im_qq / cap_im_tg / cap_im_feishu / cap_im_wechat.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *esp_websocket_client_handle_t;

/* Event op_codes  */
typedef enum {
    WEBSOCKET_EVENT_ANY          = -1,
    WEBSOCKET_EVENT_CONNECTED    = 0,
    WEBSOCKET_EVENT_DISCONNECTED = 1,
    WEBSOCKET_EVENT_DATA         = 2,
    WEBSOCKET_EVENT_ERROR        = 3,
    WEBSOCKET_EVENT_CLOSED       = 4,
} esp_websocket_event_id_t;

#define WS_TRANSPORT_OPCODES_TEXT    0x01
#define WS_TRANSPORT_OPCODES_BINARY  0x02
#define WS_TRANSPORT_OPCODES_CLOSE   0x08
#define WS_TRANSPORT_OPCODES_PING    0x09
#define WS_TRANSPORT_OPCODES_PONG    0x0A

/* Opaque transport handle (not used by caller) */
typedef void *esp_transport_handle_t;

typedef struct {
    const char               *uri;
    int                        buffer_size;
    int                        task_stack;
    int                        task_prio;
    int                        reconnect_timeout_ms;
    int                        network_timeout_ms;
    bool                       disable_auto_reconnect;
    void                     (*crt_bundle_attach)(void *);
    bool                       keep_alive_enable;
    int                        keep_alive_idle;
    int                        keep_alive_interval;
    int                        keep_alive_count;
    bool                       skip_cert_common_name_check;
    bool                       disable_pingpong_discon;
    int                        ping_interval_sec;
    int                        pingpong_timeout_sec;
    esp_transport_handle_t     transport;
    const char               *cert_pem;
    const char               *client_cert_pem;
    const char               *client_key_pem;
    const char               *headers[];
} esp_websocket_client_config_t;

typedef struct {
    esp_websocket_event_id_t  op_code;
    const char               *data_ptr;
    int                        data_len;
    int                        payload_len;
    int                        payload_offset;
    esp_websocket_client_handle_t client;
    void                     *user_data;
} esp_websocket_event_data_t;

esp_websocket_client_handle_t esp_websocket_client_init(
        const esp_websocket_client_config_t *config);

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);

esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
                                        esp_websocket_event_id_t event,
                                        esp_event_handler_t cb,
                                        void *arg);

int esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                   const char *data, int len, int timeout_ms);

int esp_websocket_client_send_bin(esp_websocket_client_handle_t client,
                                   const char *data, int len, int timeout_ms);

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client);

#ifdef __cplusplus
}
#endif
