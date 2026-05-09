/*
 * esp_websocket_client_android.c — WebSocket client for Android
 *
 * Implements esp_websocket_client API using POSIX sockets + RFC 6455.
 * For wss:// connections, loads libssl.so at runtime via dlopen.
 * Falls back to plain TCP for ws:// if SSL library is unavailable.
 *
 * Based on sim_hal/esp_websocket_client.c desktop implementation,
 * adapted for Android platform.
 */
#include "esp_websocket_client.h"
#include "esp_log.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static const char *TAG = "ws_android";

/* ---- RFC 6455 WebSocket frame opcodes ---- */
#define WS_FIN        0x80
#define WS_OP_TEXT    0x01
#define WS_OP_BIN     0x02
#define WS_OP_CLOSE   0x08
#define WS_OP_PING    0x09
#define WS_OP_PONG    0x0A

/* ---- Runtime SSL function pointers (loaded via dlopen) ---- */
typedef struct ssl_backend {
    void *libssl;
    void *libcrypto;

    void *ctx;  /* SSL_CTX* */

    void  (*SSL_library_init)(void);
    void  (*SSL_load_error_strings)(void);
    void *(*TLS_client_method)(void);
    void *(*SSL_CTX_new)(const void *method);
    void  (*SSL_CTX_free)(void *ctx);
    void  (*SSL_CTX_set_verify)(void *ctx, int mode, void *cb);
    void *(*SSL_new)(void *ctx);
    int   (*SSL_set_fd)(void *ssl, int fd);
    int   (*SSL_set_tlsext_host_name)(void *ssl, const char *name);
    int   (*SSL_connect)(void *ssl);
    int   (*SSL_read)(void *ssl, void *buf, int num);
    int   (*SSL_write)(void *ssl, const void *buf, int num);
    int   (*SSL_shutdown)(void *ssl);
    void  (*SSL_free)(void *ssl);

    bool loaded;
} ssl_backend_t;

static ssl_backend_t s_ssl = {0};
static pthread_once_t s_ssl_init_once = PTHREAD_ONCE_INIT;

static void ssl_load(void)
{
    s_ssl.libssl = dlopen("libssl.so", RTLD_LAZY);
    if (!s_ssl.libssl) {
        ESP_LOGW(TAG, "Cannot load libssl.so: %s, wss:// will not be available", dlerror());
        return;
    }
    s_ssl.libcrypto = dlopen("libcrypto.so", RTLD_LAZY);

#define SSL_FN(name) \
    do { \
        s_ssl.name = dlsym(s_ssl.libssl, #name); \
        if (!s_ssl.name) { \
            ESP_LOGW(TAG, "Missing SSL symbol: " #name); \
            dlclose(s_ssl.libssl); s_ssl.libssl = NULL; \
            if (s_ssl.libcrypto) { dlclose(s_ssl.libcrypto); s_ssl.libcrypto = NULL; } \
            return; \
        } \
    } while(0)

    SSL_FN(SSL_library_init);
    SSL_FN(SSL_load_error_strings);
    SSL_FN(TLS_client_method);
    SSL_FN(SSL_CTX_new);
    SSL_FN(SSL_CTX_free);
    SSL_FN(SSL_CTX_set_verify);
    SSL_FN(SSL_new);
    SSL_FN(SSL_set_fd);
    SSL_FN(SSL_set_tlsext_host_name);
    SSL_FN(SSL_connect);
    SSL_FN(SSL_read);
    SSL_FN(SSL_write);
    SSL_FN(SSL_shutdown);
    SSL_FN(SSL_free);
#undef SSL_FN

    s_ssl.SSL_library_init();
    s_ssl.SSL_load_error_strings();
    s_ssl.loaded = true;
    ESP_LOGI(TAG, "SSL backend loaded (libssl.so)");
}

/* ---- WebSocket client context ---- */
typedef struct ws_ctx {
    int fd;                    /* socket fd, -1 = disconnected */
    char *uri;                 /* full ws:// or wss:// URL */
    char *host;
    int  port;
    char *path;
    bool use_tls;

    /* SSL */
    void *ssl;

    volatile bool connected;
    volatile bool running;
    pthread_t recv_thread;
    pthread_mutex_t lock;
    pthread_mutex_t write_lock;

    int network_timeout_ms;
    int reconnect_timeout_ms;
    bool disable_auto_reconnect;

    /* Event callbacks */
    struct {
        esp_event_handler_t cb;
        void *arg;
    } events[8];
} ws_ctx_t;

/* ---- Forward declarations ---- */
static void ws_send_frame(ws_ctx_t *ctx, int opcode, const char *data, int len);
static void ws_fire_event(ws_ctx_t *ctx, esp_websocket_event_id_t event_id,
                          int opcode, const char *data, int data_len,
                          int payload_len, int payload_offset);

/* ---- URL parsing ---- */
static bool ws_parse_url(ws_ctx_t *ctx)
{
    const char *uri = ctx->uri;
    if (!uri) return false;

    if (strncmp(uri, "wss://", 6) == 0) {
        ctx->use_tls = true;
        ctx->port = 443;
        uri += 6;
    } else if (strncmp(uri, "ws://", 5) == 0) {
        ctx->use_tls = false;
        ctx->port = 80;
        uri += 5;
    } else {
        ESP_LOGE(TAG, "Unsupported URI scheme: %s", ctx->uri);
        return false;
    }

    const char *slash = strchr(uri, '/');
    const char *colon = strchr(uri, ':');

    if (colon && (!slash || colon < slash)) {
        size_t hl = (size_t)(colon - uri);
        ctx->host = strndup(uri, hl);
        ctx->port = atoi(colon + 1);
    } else if (slash) {
        size_t hl = (size_t)(slash - uri);
        ctx->host = strndup(uri, hl);
    } else {
        ctx->host = strdup(uri);
    }

    ctx->path = slash ? strdup(slash) : strdup("/");
    return ctx->host && ctx->path;
}

/* ---- Base64 encode (no padding, for WS key) ---- */
static char *ws_base64_key(const unsigned char *input, int len)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int out_len = ((len + 2) / 3) * 4;
    char *out = calloc(1, (size_t)out_len + 1);
    if (!out) return NULL;

    int i = 0, j = 0;
    while (i < len) {
        unsigned int a = (unsigned int)(input[i++]);
        unsigned int b = (unsigned int)(i < len ? input[i++] : 0);
        unsigned int c = (unsigned int)(i < len ? input[i++] : 0);
        unsigned int triple = (a << 16) | (b << 8) | c;
        out[j++] = b64[(triple >> 18) & 0x3F];
        out[j++] = b64[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64[triple & 0x3F];
    }
    out[out_len] = '\0';
    return out;
}

/* ---- Raw send/recv (with optional TLS) ---- */
static int ws_send_raw(ws_ctx_t *ctx, const void *buf, size_t len)
{
    if (ctx->use_tls && ctx->ssl) {
        return s_ssl.SSL_write(ctx->ssl, buf, (int)len);
    }
    return (int)send(ctx->fd, buf, len, 0);
}

static int ws_recv_raw(ws_ctx_t *ctx, void *buf, size_t len)
{
    if (ctx->use_tls && ctx->ssl) {
        return s_ssl.SSL_read(ctx->ssl, buf, (int)len);
    }
    return (int)recv(ctx->fd, buf, len, 0);
}

/* ---- TCP connect ---- */
static int ws_tcp_connect(ws_ctx_t *ctx)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", ctx->port);

    int rc = getaddrinfo(ctx->host, port_str, &hints, &res);
    if (rc != 0 || !res) {
        ESP_LOGE(TAG, "getaddrinfo(%s:%d) failed: %s", ctx->host, ctx->port, gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec = ctx->network_timeout_ms / 1000;
        tv.tv_usec = (ctx->network_timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;

        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
    return fd;
}

/* ---- TLS connect ---- */
static bool ws_tls_connect(ws_ctx_t *ctx)
{
    if (!s_ssl.loaded) {
        ESP_LOGE(TAG, "SSL not available for wss://");
        return false;
    }

    s_ssl.ctx = s_ssl.SSL_CTX_new(s_ssl.TLS_client_method());
    if (!s_ssl.ctx) {
        ESP_LOGE(TAG, "SSL_CTX_new failed");
        return false;
    }

    s_ssl.SSL_CTX_set_verify(s_ssl.ctx, 0, NULL);
    ctx->ssl = s_ssl.SSL_new(s_ssl.ctx);
    if (!ctx->ssl) {
        ESP_LOGE(TAG, "SSL_new failed");
        return false;
    }

    s_ssl.SSL_set_fd(ctx->ssl, ctx->fd);
    s_ssl.SSL_set_tlsext_host_name(ctx->ssl, ctx->host);

    int rc = s_ssl.SSL_connect(ctx->ssl);
    if (rc != 1) {
        ESP_LOGE(TAG, "SSL_connect failed: rc=%d", rc);
        return false;
    }

    ESP_LOGI(TAG, "TLS connected to %s:%d", ctx->host, ctx->port);
    return true;
}

/* ---- WebSocket handshake ---- */
static bool ws_handshake(ws_ctx_t *ctx)
{
    unsigned char key_bytes[16];
    for (int i = 0; i < 16; i++) key_bytes[i] = (unsigned char)(rand() & 0xFF);
    char *b64_key = ws_base64_key(key_bytes, 16);
    if (!b64_key) return false;

    char req[4096];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             ctx->path, ctx->host, ctx->port, b64_key);
    free(b64_key);

    if (ws_send_raw(ctx, req, strlen(req)) < 0) {
        ESP_LOGE(TAG, "Failed to send handshake");
        return false;
    }

    char resp[4096];
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        int n = ws_recv_raw(ctx, resp + total, (size_t)(sizeof(resp) - 1 - total));
        if (n <= 0) {
            ESP_LOGE(TAG, "Handshake recv failed: %s", n == 0 ? "EOF" : strerror(errno));
            return false;
        }
        total += n;
        resp[total] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
    }

    if (strstr(resp, "101") == NULL) {
        ESP_LOGE(TAG, "Handshake failed (expected 101): %s", resp);
        return false;
    }

    ESP_LOGI(TAG, "WebSocket handshake OK (TLS=%d)", ctx->use_tls);
    return true;
}

/* ---- WebSocket frame receive ---- */
static int ws_recv_frame(ws_ctx_t *ctx, char *buf, int buf_size,
                         int *out_opcode, int *out_payload_len)
{
    unsigned char header[2];
    int n = ws_recv_raw(ctx, header, 2);
    if (n != 2) return -1;

    int opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t plen = header[1] & 0x7F;

    if (plen == 126) {
        unsigned char ext[2];
        if (ws_recv_raw(ctx, ext, 2) != 2) return -1;
        plen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (plen == 127) {
        unsigned char ext[8];
        if (ws_recv_raw(ctx, ext, 8) != 8) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | ext[i];
    }

    unsigned char mask_key[4] = {0};
    if (masked) {
        if (ws_recv_raw(ctx, mask_key, 4) != 4) return -1;
    }

    if (plen > (uint64_t)buf_size) plen = (uint64_t)buf_size;

    int read = 0;
    while ((uint64_t)read < plen) {
        n = ws_recv_raw(ctx, buf + read, (int)(plen - (uint64_t)read));
        if (n <= 0) return -1;
        read += n;
    }

    if (masked) {
        for (int i = 0; i < read; i++)
            buf[i] ^= mask_key[i & 3];
    }

    *out_opcode = opcode;
    *out_payload_len = read;
    return 0;
}

/* ---- WebSocket frame send ---- */
static void ws_send_frame(ws_ctx_t *ctx, int opcode, const char *data, int len)
{
    unsigned char header[10];
    int hdr_len;
    unsigned char mask_key[4] = {0x01, 0x02, 0x03, 0x04};

    header[0] = (unsigned char)(WS_FIN | opcode);

    if (len < 126) {
        header[1] = (unsigned char)(len | 0x80);
        hdr_len = 2;
    } else if (len < 65536) {
        header[1] = (unsigned char)(126 | 0x80);
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        hdr_len = 4;
    } else {
        header[1] = (unsigned char)(127 | 0x80);
        for (int i = 0; i < 8; i++)
            header[2 + i] = (unsigned char)((len >> (56 - i * 8)) & 0xFF);
        hdr_len = 10;
    }
    memcpy(header + hdr_len, mask_key, 4);
    hdr_len += 4;

    pthread_mutex_lock(&ctx->write_lock);
    ws_send_raw(ctx, header, (size_t)hdr_len);
    if (data && len > 0) {
        unsigned char *masked = malloc((size_t)len);
        if (masked) {
            for (int i = 0; i < len; i++) masked[i] = (unsigned char)data[i] ^ mask_key[i % 4];
            ws_send_raw(ctx, masked, (size_t)len);
            free(masked);
        }
    }
    pthread_mutex_unlock(&ctx->write_lock);
}

/* ---- Event dispatching ---- */
static void ws_fire_event(ws_ctx_t *ctx, esp_websocket_event_id_t event_id,
                          int opcode, const char *data, int data_len,
                          int payload_len, int payload_offset)
{
    esp_event_handler_t cb = NULL;
    void *arg = NULL;

    pthread_mutex_lock(&ctx->lock);
    if (ctx->events[0].cb) { cb = ctx->events[0].cb; arg = ctx->events[0].arg; }
    size_t idx = (size_t)(event_id + 2);
    if (idx < 8 && ctx->events[idx].cb) {
        cb  = ctx->events[idx].cb;
        arg = ctx->events[idx].arg;
    }
    pthread_mutex_unlock(&ctx->lock);

    if (cb) {
        esp_websocket_event_data_t d = {0};
        d.op_code        = opcode;
        d.data_ptr       = data;
        d.data_len       = data_len;
        d.payload_len    = payload_len;
        d.payload_offset = payload_offset;
        d.client         = ctx;
        d.user_data      = arg;
        cb(arg, "websocket", (int32_t)event_id, &d);
    }
}

/* ---- Connection management ---- */
static bool ws_connect_once(ws_ctx_t *ctx)
{
    ctx->fd = ws_tcp_connect(ctx);
    if (ctx->fd < 0) {
        ESP_LOGW(TAG, "TCP connect failed to %s:%d", ctx->host, ctx->port);
        return false;
    }

    if (ctx->use_tls) {
        if (!ws_tls_connect(ctx)) {
            ESP_LOGW(TAG, "TLS connect failed");
            close(ctx->fd);
            ctx->fd = -1;
            return false;
        }
    }

    if (!ws_handshake(ctx)) {
        ESP_LOGW(TAG, "WebSocket handshake failed");
        if (ctx->use_tls && ctx->ssl) {
            s_ssl.SSL_shutdown(ctx->ssl);
            s_ssl.SSL_free(ctx->ssl);
            ctx->ssl = NULL;
        }
        if (s_ssl.ctx) {
            s_ssl.SSL_CTX_free(s_ssl.ctx);
            s_ssl.ctx = NULL;
        }
        close(ctx->fd);
        ctx->fd = -1;
        return false;
    }

    ctx->connected = true;
    ESP_LOGI(TAG, "WebSocket connected to %s (TLS=%d)", ctx->uri, ctx->use_tls);
    ws_fire_event(ctx, WEBSOCKET_EVENT_CONNECTED, 0, NULL, 0, 0, 0);
    return true;
}

static void ws_disconnect(ws_ctx_t *ctx)
{
    ctx->connected = false;
    if (ctx->use_tls && ctx->ssl) {
        s_ssl.SSL_shutdown(ctx->ssl);
        s_ssl.SSL_free(ctx->ssl);
        ctx->ssl = NULL;
    }
    if (s_ssl.ctx) {
        s_ssl.SSL_CTX_free(s_ssl.ctx);
        s_ssl.ctx = NULL;
    }
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    ws_fire_event(ctx, WEBSOCKET_EVENT_DISCONNECTED, 0, NULL, 0, 0, 0);
}

/* ---- Receive thread ---- */
static void *ws_recv_thread(void *arg)
{
    ws_ctx_t *ctx = (ws_ctx_t *)arg;

    while (ctx->running) {
        while (ctx->running && !ctx->connected) {
            if (ws_connect_once(ctx)) break;
            if (ctx->disable_auto_reconnect) {
                ESP_LOGI(TAG, "Auto-reconnect disabled, exiting recv thread");
                return NULL;
            }
            int delay = ctx->reconnect_timeout_ms > 0 ? ctx->reconnect_timeout_ms : 5000;
            usleep((unsigned)delay * 1000);
        }

        if (!ctx->connected) continue;

        char buf[65536];
        while (ctx->running && ctx->connected) {
            int opcode = 0, payload_len = 0;
            int rc = ws_recv_frame(ctx, buf, (int)sizeof(buf) - 1,
                                   &opcode, &payload_len);
            if (rc < 0) {
                ESP_LOGW(TAG, "recv_frame failed, disconnecting");
                ws_disconnect(ctx);
                break;
            }

            buf[payload_len] = '\0';

            switch (opcode) {
            case WS_OP_TEXT:
                ws_fire_event(ctx, WEBSOCKET_EVENT_DATA, opcode,
                              buf, payload_len, payload_len, 0);
                break;
            case WS_OP_PING:
                ws_send_frame(ctx, WS_OP_PONG, buf, payload_len);
                break;
            case WS_OP_PONG:
                break;
            case WS_OP_CLOSE:
                ws_send_frame(ctx, WS_OP_CLOSE, NULL, 0);
                ws_disconnect(ctx);
                break;
            default:
                break;
            }
        }
    }

    return NULL;
}

/* ================================================================
 * Public API
 * ================================================================ */

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config)
{
    if (!config || !config->uri) return NULL;

    pthread_once(&s_ssl_init_once, ssl_load);

    ws_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->uri                    = strdup(config->uri);
    ctx->network_timeout_ms     = config->network_timeout_ms > 0 ? config->network_timeout_ms : 10000;
    ctx->reconnect_timeout_ms   = config->reconnect_timeout_ms > 0 ? config->reconnect_timeout_ms : 5000;
    ctx->disable_auto_reconnect = config->disable_auto_reconnect;
    ctx->fd                     = -1;

    pthread_mutex_init(&ctx->lock, NULL);
    pthread_mutex_init(&ctx->write_lock, NULL);

    if (!ws_parse_url(ctx)) {
        ESP_LOGE(TAG, "Failed to parse WS URI: %s", config->uri);
        free(ctx->uri);
        pthread_mutex_destroy(&ctx->lock);
        pthread_mutex_destroy(&ctx->write_lock);
        free(ctx);
        return NULL;
    }

    /* Warn if wss:// requested but SSL not available */
    if (ctx->use_tls && !s_ssl.loaded) {
        ESP_LOGW(TAG, "wss:// requested but SSL not available, connection will fail");
    }

    ESP_LOGI(TAG, "WS init: %s (host=%s port=%d path=%s TLS=%d)",
             ctx->uri, ctx->host, ctx->port, ctx->path, ctx->use_tls);
    return (esp_websocket_client_handle_t)ctx;
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client)
{
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    if (!ctx) return ESP_ERR_INVALID_ARG;
    if (ctx->connected) return ESP_OK;

    ctx->running = true;
    int rc = pthread_create(&ctx->recv_thread, NULL, ws_recv_thread, ctx);
    if (rc != 0) {
        ctx->running = false;
        ESP_LOGE(TAG, "Failed to create recv thread: %s", strerror(rc));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WS start: recv thread running");
    return ESP_OK;
}

esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client)
{
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    ctx->running = false;
    ws_disconnect(ctx);

    if (ctx->recv_thread) {
        pthread_join(ctx->recv_thread, NULL);
        memset(&ctx->recv_thread, 0, sizeof(ctx->recv_thread));
    }

    return ESP_OK;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client)
{
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    esp_websocket_client_stop(client);

    pthread_mutex_destroy(&ctx->lock);
    pthread_mutex_destroy(&ctx->write_lock);

    free(ctx->uri);
    free(ctx->host);
    free(ctx->path);
    free(ctx);
    return ESP_OK;
}

esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
                                        esp_websocket_event_id_t event,
                                        esp_event_handler_t cb,
                                        void *arg)
{
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&ctx->lock);
    size_t idx;
    if (event == WEBSOCKET_EVENT_ANY) {
        idx = 0;
    } else {
        idx = (size_t)(event + 2);
    }
    if (idx < 8) {
        ctx->events[idx].cb  = cb;
        ctx->events[idx].arg = arg;
    }
    pthread_mutex_unlock(&ctx->lock);
    return ESP_OK;
}

int esp_websocket_client_send_text(esp_websocket_client_handle_t client,
                                    const char *data, int len, int timeout_ms)
{
    (void)timeout_ms;
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    if (!ctx || !ctx->connected || !data) return -1;
    if (len < 0) len = (int)strlen(data);
    ws_send_frame(ctx, WS_OP_TEXT, data, len);
    return len;
}

int esp_websocket_client_send_bin(esp_websocket_client_handle_t client,
                                   const char *data, int len, int timeout_ms)
{
    (void)timeout_ms;
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    if (!ctx || !ctx->connected || !data) return -1;
    if (len < 0) return -1;
    ws_send_frame(ctx, WS_OP_BIN, data, len);
    return len;
}

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client)
{
    ws_ctx_t *ctx = (ws_ctx_t *)client;
    return ctx && ctx->connected;
}
