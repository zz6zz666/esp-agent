/*
 * esp_websocket_client.c — POSIX WebSocket client for desktop simulator
 *
 * Implements the esp_websocket_client ESP-IDF API subset used by
 * cap_im_qq / cap_im_tg / cap_im_feishu / cap_im_wechat.
 *
 * Uses POSIX sockets + OpenSSL (for wss://) + pthreads.
 * WebSocket framing per RFC 6455.
 */
#include "esp/esp_websocket_client.h"
#include "esp/esp_log.h"

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <bcrypt.h>
# include <io.h>
# pragma comment(lib, "ws2_32.lib")
# pragma comment(lib, "bcrypt.lib")
#else
# include <arpa/inet.h>
# include <errno.h>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <sys/select.h>
# include <sys/socket.h>
# include <sys/time.h>
# include <sys/types.h>
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(PLATFORM_WINDOWS)
#ifndef strndup
static inline char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *d = malloc(len + 1);
    if (d) {
        memcpy(d, s, len);
        d[len] = '\0';
    }
    return d;
}
#endif
#endif

/* Platform socket type and helpers */
#if defined(PLATFORM_WINDOWS)
typedef SOCKET ws_socket_t;
# define WS_INVALID_SOCKET   INVALID_SOCKET
# define WS_CLOSE_SOCKET(s)  closesocket(s)
# define WS_LAST_ERROR()     WSAGetLastError()
# define WS_EINPROGRESS      WSAEWOULDBLOCK
# define WS_SEND_FLAGS       0
#else
typedef int ws_socket_t;
# define WS_INVALID_SOCKET   (-1)
# define WS_CLOSE_SOCKET(s)  close(s)
# define WS_LAST_ERROR()     errno
# define WS_EINPROGRESS      EINPROGRESS
# define WS_SEND_FLAGS       MSG_NOSIGNAL
#endif

static const char *TAG = "ws_client";

/* ---- WebSocket frame opcodes ---- */
#define WS_OP_CONT  0x0
#define WS_OP_TEXT  0x1
#define WS_OP_CLOSE 0x8
#define WS_OP_PING  0x9
#define WS_OP_PONG  0xA

/* ---- Internal context ---- */
typedef struct {
    char          *uri;             /* full ws:// or wss:// URL */
    char          *host;
    char          *path;
    int            port;
    bool           use_tls;
    int            buffer_size;
    int            reconnect_timeout_ms;
    int            network_timeout_ms;
    bool           disable_auto_reconnect;

    /* thread-safety */
    pthread_mutex_t mutex;

    /* connection state */
    volatile bool  running;        /* set by start, cleared by stop */
    volatile bool  connected;
    ws_socket_t    sock_fd;
    SSL           *ssl;
    SSL_CTX       *ssl_ctx;
    pthread_t      recv_thread;

    /* write mutex (so send is MT-safe) */
    pthread_mutex_t write_mutex;

    /* event callback */
    struct {
        esp_event_handler_t cb;
        void *arg;
    } events[8];  /* indexed by opcode */

    void          *user_context;
} ws_ctx_t;

/* ---- helpers ---- */

static void ws_fire_event(ws_ctx_t *ctx, esp_websocket_event_id_t event_id,
                          int transport_opcode,
                          const char *data, int data_len,
                          int payload_len, int payload_offset)
{
    esp_event_handler_t cb = NULL;
    void *arg = NULL;

    pthread_mutex_lock(&ctx->mutex);
    /* ANY handler */
    if (ctx->events[0].cb) { cb = ctx->events[0].cb; arg = ctx->events[0].arg; }
    /* specific handler */
    size_t idx = (size_t)(event_id + 2);
    if (idx < 8 && ctx->events[idx].cb) {
        cb  = ctx->events[idx].cb;
        arg = ctx->events[idx].arg;
    }
    pthread_mutex_unlock(&ctx->mutex);

    if (cb) {
        esp_websocket_event_data_t d = {0};
        d.op_code        = transport_opcode;
        d.data_ptr       = data;
        d.data_len       = data_len;
        d.payload_len    = payload_len;
        d.payload_offset = payload_offset;
        d.client         = ctx;
        d.user_data      = arg;
        /* ESP-IDF event-loop callback signature:
           handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) */
        cb(arg, "websocket", (int32_t)event_id, &d);
    }
}

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
        return false;
    }

    /* host:port/path */
    const char *slash = strchr(uri, '/');
    const char *colon = strchr(uri, ':');

    if (colon && (!slash || colon < slash)) {
        /* explicit port */
        size_t host_len = (size_t)(colon - uri);
        ctx->host = strndup(uri, host_len);
        ctx->port = atoi(colon + 1);
    } else if (slash) {
        size_t host_len = (size_t)(slash - uri);
        ctx->host = strndup(uri, host_len);
    } else {
        ctx->host = strdup(uri);
    }

    if (slash)
        ctx->path = strdup(slash);
    else
        ctx->path = strdup("/");

    return ctx->host && ctx->path;
}

static ws_socket_t ws_tcp_connect(ws_ctx_t *ctx)
{
    struct addrinfo hints = {0}, *res;
    char port_str[16];

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", ctx->port);
    int rc = getaddrinfo(ctx->host, port_str, &hints, &res);
    if (rc != 0) {
        ESP_LOGE(TAG, "getaddrinfo(%s:%d): %s", ctx->host, ctx->port,
#if defined(PLATFORM_WINDOWS)
                 gai_strerrorA(rc));
#else
                 gai_strerror(rc));
#endif
        return WS_INVALID_SOCKET;
    }

    ws_socket_t fd = WS_INVALID_SOCKET;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == WS_INVALID_SOCKET) continue;

        /* set non-blocking for connect timeout */
#if defined(PLATFORM_WINDOWS)
        unsigned long nonblock = 1;
        ioctlsocket(fd, FIONBIO, &nonblock);
#else
        long flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

        rc = connect(fd, rp->ai_addr, (int)rp->ai_addrlen);
        if (rc < 0 && WS_LAST_ERROR() != WS_EINPROGRESS) {
            WS_CLOSE_SOCKET(fd); fd = WS_INVALID_SOCKET; continue;
        }

        if (rc == 0) {
            /* immediate connect — restore blocking */
#if defined(PLATFORM_WINDOWS)
            nonblock = 0;
            ioctlsocket(fd, FIONBIO, &nonblock);
#else
            fcntl(fd, F_SETFL, flags);
#endif
            break;
        }

        /* wait for connect */
        fd_set wfds;
        struct timeval tv = {
            .tv_sec  = (long)((unsigned)ctx->network_timeout_ms / 1000),
            .tv_usec = (long)((unsigned)(ctx->network_timeout_ms % 1000) * 1000),
        };
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        rc = select((int)(fd + 1), NULL, &wfds, NULL, &tv);
        if (rc <= 0) { WS_CLOSE_SOCKET(fd); fd = WS_INVALID_SOCKET; continue; }

        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR,
#if defined(PLATFORM_WINDOWS)
                   (char *)&err,
#else
                   &err,
#endif
                   &len);
        if (err != 0) { WS_CLOSE_SOCKET(fd); fd = WS_INVALID_SOCKET; continue; }

        /* restore blocking */
#if defined(PLATFORM_WINDOWS)
        nonblock = 0;
        ioctlsocket(fd, FIONBIO, &nonblock);
#else
        fcntl(fd, F_SETFL, flags);
#endif
        break;
    }

    freeaddrinfo(res);

    if (fd != WS_INVALID_SOCKET) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
#if defined(PLATFORM_WINDOWS)
                   (const char *)&one,
#else
                   &one,
#endif
                   sizeof(one));
    }

    return fd;
}

static bool ws_tls_connect(ws_ctx_t *ctx)
{
    ctx->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx->ssl_ctx) return false;

    SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_NONE, NULL);
    ctx->ssl = SSL_new(ctx->ssl_ctx);
    if (!ctx->ssl) return false;

    SSL_set_fd(ctx->ssl, (int)(intptr_t)ctx->sock_fd);
    SSL_set_tlsext_host_name(ctx->ssl, ctx->host);

    int rc = SSL_connect(ctx->ssl);
    return rc == 1;
}

static int ws_send_raw(ws_ctx_t *ctx, const void *buf, size_t len)
{
    if (ctx->use_tls) {
        return SSL_write(ctx->ssl, buf, (int)len);
    }
    return (int)send(ctx->sock_fd, buf,
#if defined(PLATFORM_WINDOWS)
                     (int)len,
#else
                     len,
#endif
                     WS_SEND_FLAGS);
}

static int ws_recv_raw(ws_ctx_t *ctx, void *buf, size_t len)
{
    if (ctx->use_tls) {
        return SSL_read(ctx->ssl, buf, (int)len);
    }
    return (int)recv(ctx->sock_fd, buf,
#ifdef PLATFORM_WINDOWS
                     (int)len,
#else
                     len,
#endif
                     0);
}

/* ---- WebSocket handshake ---- */

static bool ws_handshake(ws_ctx_t *ctx)
{
    char req[4096];
    char key_b64[64] = {0};
    unsigned char key_bytes[16];

    /* generate random key */
#ifdef PLATFORM_WINDOWS
    BCryptGenRandom(NULL, key_bytes, sizeof(key_bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    FILE *urand = fopen("/dev/urandom", "r");
    if (urand) { fread(key_bytes, 1, 16, urand); fclose(urand); }
    else {
        for (int i = 0; i < 16; i++) key_bytes[i] = (unsigned char)(rand() & 0xFF);
    }
#endif

    /* simple base64 (no padding for WS key) */
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int ki = 0;
    for (int i = 0; i < 16; i += 3) {
        unsigned long v = ((unsigned long)key_bytes[i] << 16) |
                          ((unsigned long)(i+1 < 16 ? key_bytes[i+1] : 0) << 8) |
                          (unsigned long)(i+2 < 16 ? key_bytes[i+2] : 0);
        key_b64[ki++] = b64[(v >> 18) & 0x3F];
        key_b64[ki++] = b64[(v >> 12) & 0x3F];
        key_b64[ki++] = (i+1 < 16) ? b64[(v >> 6) & 0x3F] : '=';
        key_b64[ki++] = (i+2 < 16) ? b64[v & 0x3F] : '=';
    }

    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             ctx->path, ctx->host, key_b64);

    if (ws_send_raw(ctx, req, strlen(req)) < 0) {
        ESP_LOGE(TAG, "Failed to send handshake");
        return false;
    }

    /* read response */
    char resp[4096];
    int total = 0;
    int n;
    while (total < (int)sizeof(resp) - 1) {
        n = ws_recv_raw(ctx, resp + total, sizeof(resp) - 1 - (size_t)total);
        if (n <= 0) { ESP_LOGE(TAG, "Handshake recv failed"); return false; }
        total += n;
        resp[total] = '\0';
        if (strstr(resp, "\r\n\r\n")) break;
    }

    /* check for 101 */
    if (strstr(resp, "101") == NULL) {
        ESP_LOGE(TAG, "Handshake response: %s", resp);
        return false;
    }

    return true;
}

/* ---- WebSocket frame I/O ---- */

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

    *out_opcode     = opcode;
    *out_payload_len = read;
    return 0;
}

static int ws_send_frame(ws_ctx_t *ctx, int opcode,
                         const void *data, size_t len)
{
    unsigned char frame[10 + 4];  /* header + mask key */
    size_t hlen = 2;

    frame[0] = (unsigned char)(0x80 | opcode);  /* FIN + opcode */

    if (len < 126) {
        frame[1] = (unsigned char)(0x80 | len); /* masked */
    } else if (len <= 0xFFFF) {
        frame[1] = (unsigned char)(0x80 | 126);
        frame[2] = (unsigned char)((len >> 8) & 0xFF);
        frame[3] = (unsigned char)(len & 0xFF);
        hlen = 4;
    } else {
        frame[1] = (unsigned char)(0x80 | 127);
        for (int i = 0; i < 8; i++)
            frame[2 + i] = (unsigned char)((len >> (56 - i * 8)) & 0xFF);
        hlen = 10;
    }

    /* mask key (client-to-server must mask) */
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (unsigned char)(rand() & 0xFF);
    memcpy(&frame[hlen], mask, 4);
    hlen += 4;

    /* copy and mask payload */
    unsigned char *sendbuf = malloc(hlen + len);
    if (!sendbuf) return -1;
    memcpy(sendbuf, frame, hlen);
    for (size_t i = 0; i < len; i++)
        sendbuf[hlen + i] = ((const unsigned char *)data)[i] ^ mask[i & 3];

    /* thread-safe write */
    pthread_mutex_lock(&ctx->write_mutex);
    int sent = ws_send_raw(ctx, sendbuf, hlen + len);
    pthread_mutex_unlock(&ctx->write_mutex);

    free(sendbuf);
    if (sent < 0) return -1;
    return (int)len;
}

/* ---- receive thread ---- */

static bool ws_connect(ws_ctx_t *ctx)
{
    ctx->sock_fd = ws_tcp_connect(ctx);
    if (ctx->sock_fd == WS_INVALID_SOCKET) {
        ESP_LOGW(TAG, "TCP connect failed to %s:%d", ctx->host, ctx->port);
        return false;
    }

    if (ctx->use_tls) {
        if (!ws_tls_connect(ctx)) {
            ESP_LOGW(TAG, "TLS connect failed");
            WS_CLOSE_SOCKET(ctx->sock_fd);
            ctx->sock_fd = WS_INVALID_SOCKET;
            return false;
        }
    }

    if (!ws_handshake(ctx)) {
        ESP_LOGW(TAG, "WebSocket handshake failed");
        if (ctx->use_tls && ctx->ssl)      { SSL_shutdown(ctx->ssl); SSL_free(ctx->ssl); ctx->ssl = NULL; }
        if (ctx->ssl_ctx)                  { SSL_CTX_free(ctx->ssl_ctx); ctx->ssl_ctx = NULL; }
        WS_CLOSE_SOCKET(ctx->sock_fd);
        ctx->sock_fd = WS_INVALID_SOCKET;
        return false;
    }

    ctx->connected = true;
    ESP_LOGI(TAG, "WebSocket connected to %s (fd=%d)", ctx->uri, (int)(intptr_t)ctx->sock_fd);
    ws_fire_event(ctx, WEBSOCKET_EVENT_CONNECTED, 0, NULL, 0, 0, 0);
    return true;
}

static void ws_disconnect(ws_ctx_t *ctx)
{
    ctx->connected = false;
    if (ctx->ssl)      { SSL_shutdown(ctx->ssl); SSL_free(ctx->ssl); ctx->ssl = NULL; }
    if (ctx->ssl_ctx)  { SSL_CTX_free(ctx->ssl_ctx); ctx->ssl_ctx = NULL; }
    if (ctx->sock_fd != WS_INVALID_SOCKET) { WS_CLOSE_SOCKET(ctx->sock_fd); ctx->sock_fd = WS_INVALID_SOCKET; }
    ws_fire_event(ctx, WEBSOCKET_EVENT_DISCONNECTED, 0, NULL, 0, 0, 0);
}

static void *ws_recv_thread(void *arg)
{
    ws_ctx_t *ctx = arg;
    int reconnect_delay = ctx->reconnect_timeout_ms > 0 ?
                          ctx->reconnect_timeout_ms : 3000;
    (void)reconnect_delay;

    while (ctx->running) {
        /* connect (with retry) */
        while (ctx->running && !ctx->connected) {
            if (ws_connect(ctx)) break;
            if (ctx->disable_auto_reconnect) {
                /* don't retry — exit thread */
                return NULL;
            }
            /* back off */
            usleep((unsigned)reconnect_delay * 1000);
        }

        if (!ctx->connected) continue;

        /* read loop */
        char buf[65536];
        while (ctx->running && ctx->connected) {
            int opcode = 0, payload_len = 0;
            int rc = ws_recv_frame(ctx, buf, (int)sizeof(buf) - 1,
                                   &opcode, &payload_len);
            if (rc < 0) {
                /* connection lost */
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
                ws_send_frame(ctx, WS_OP_PONG, buf, (size_t)payload_len);
                break;
            case WS_OP_PONG:
                /* ignore */
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

/* ---- public API ---- */

esp_websocket_client_handle_t esp_websocket_client_init(
        const esp_websocket_client_config_t *config)
{
    if (!config || !config->uri) return NULL;

    ws_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->uri                    = strdup(config->uri);
    ctx->buffer_size            = config->buffer_size > 0 ? config->buffer_size : 4096;
    ctx->reconnect_timeout_ms   = config->reconnect_timeout_ms > 0 ? config->reconnect_timeout_ms : 5000;
    ctx->network_timeout_ms     = config->network_timeout_ms > 0 ? config->network_timeout_ms : 10000;
    ctx->disable_auto_reconnect = config->disable_auto_reconnect;
    ctx->sock_fd                = WS_INVALID_SOCKET;

    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_mutex_init(&ctx->write_mutex, NULL);

    if (!ws_parse_url(ctx)) {
        ESP_LOGE(TAG, "Failed to parse WS URI: %s", config->uri);
        free(ctx->uri);
        free(ctx);
        return NULL;
    }

    return ctx;
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client_)
{
    ws_ctx_t *ctx = client_;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    ctx->running = true;
    int rc = pthread_create(&ctx->recv_thread, NULL, ws_recv_thread, ctx);
    if (rc != 0) {
        ctx->running = false;
        ESP_LOGE(TAG, "Failed to create recv thread");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client_)
{
    ws_ctx_t *ctx = client_;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    ctx->running = false;
    ws_disconnect(ctx);

    if (ctx->recv_thread) {
        pthread_join(ctx->recv_thread, NULL);
        memset(&ctx->recv_thread, 0, sizeof(ctx->recv_thread));
    }

    return ESP_OK;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client_)
{
    ws_ctx_t *ctx = client_;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    esp_websocket_client_stop(client_);

    pthread_mutex_destroy(&ctx->mutex);
    pthread_mutex_destroy(&ctx->write_mutex);

    free(ctx->uri);
    free(ctx->host);
    free(ctx->path);
    free(ctx);
    return ESP_OK;
}

esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client_,
                                        esp_websocket_event_id_t event,
                                        esp_event_handler_t cb,
                                        void *arg)
{
    ws_ctx_t *ctx = client_;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    pthread_mutex_lock(&ctx->mutex);
    size_t idx;
    if (event == WEBSOCKET_EVENT_ANY) {
        idx = 0;
    } else {
        idx = (size_t)(event + 2); /* map 0..4 to 2..6 */
    }
    if (idx < 8) {
        ctx->events[idx].cb  = cb;
        ctx->events[idx].arg = arg;
    }
    pthread_mutex_unlock(&ctx->mutex);
    return ESP_OK;
}

int esp_websocket_client_send_text(esp_websocket_client_handle_t client_,
                                   const char *data, int len, int timeout_ms)
{
    (void)timeout_ms;
    ws_ctx_t *ctx = client_;
    if (!ctx || !ctx->connected || !data) return -1;
    if (len < 0) len = (int)strlen(data);
    return ws_send_frame(ctx, WS_OP_TEXT, data, (size_t)len);
}

int esp_websocket_client_send_bin(esp_websocket_client_handle_t client_,
                                   const char *data, int len, int timeout_ms)
{
    (void)timeout_ms;
    ws_ctx_t *ctx = client_;
    if (!ctx || !ctx->connected || !data) return -1;
    if (len < 0) return -1;
    return ws_send_frame(ctx, 0x2 /* binary */, data, (size_t)len);
}

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client_)
{
    ws_ctx_t *ctx = client_;
    return ctx && ctx->connected;
}
