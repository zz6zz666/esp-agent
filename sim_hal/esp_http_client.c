/*
 * esp_http_client.c — POSIX / libcurl implementation
 *
 * Replaces ESP-IDF's esp_http_client with libcurl, matching the API subset
 * used by cap_im_qq, cap_im_tg, cap_im_feishu, cap_im_wechat, and
 * cap_im_attachment.
 */
#include "esp/esp_http_client.h"
#include "esp/esp_log.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_curl";

typedef struct {
    CURL          *easy;
    struct curl_slist *headers;
    char          *post_data;
    int            post_len;
    int            status_code;
    esp_http_client_method_t method;
    char          *url;
    /* event callback */
    esp_http_client_event_handler_t event_handler;
    void          *user_data;
    /* response body accumulator (for event dispatch) */
    char          *resp_buf;
    size_t         resp_len;
    size_t         resp_cap;
    /* fire CONNECTED once */
    bool           connected_fired;

    /* ---- streaming mode ---- */
    bool           streaming;     /* true between open() and close() */
    bool           is_upload;     /* open() had write_len > 0 */
    char          *write_buf;     /* accumulated write() data */
    size_t         write_len;     /* used length */
    size_t         write_cap;     /* allocated size */
    size_t         resp_read_pos; /* read cursor for streaming downloads */
} http_ctx_t;

/* ---- curl write callback ---- */
static size_t on_data(void *ptr, size_t sz, size_t nmemb, void *user)
{
    http_ctx_t       *ctx = user;
    size_t            total = sz * nmemb;
    esp_http_client_event_t evt = {0};

    if (!ctx->connected_fired) {
        ctx->connected_fired = true;
        evt.event_id = HTTP_EVENT_ON_CONNECTED;
        evt.client   = ctx;
        evt.user_data = ctx->user_data;
        if (ctx->event_handler) ctx->event_handler(&evt);
    }

    evt.event_id = HTTP_EVENT_ON_DATA;
    evt.client   = ctx;
    evt.user_data = ctx->user_data;
    evt.data     = ptr;
    evt.data_len = (int)total;
    if (ctx->event_handler) ctx->event_handler(&evt);

    /* Also buffer for streaming read() API */
    if (ctx->streaming && !ctx->is_upload) {
        size_t need = ctx->resp_len + total + 1;
        if (need > ctx->resp_cap) {
            size_t new_cap = ctx->resp_cap ? ctx->resp_cap * 2 : 4096;
            if (new_cap < need) new_cap = need;
            char *nb = realloc(ctx->resp_buf, new_cap);
            if (nb) { ctx->resp_buf = nb; ctx->resp_cap = new_cap; }
        }
        if (ctx->resp_buf && ctx->resp_len + total <= ctx->resp_cap) {
            memcpy(ctx->resp_buf + ctx->resp_len, ptr, total);
            ctx->resp_len += total;
        }
    }

    return total;
}

static size_t on_header(void *ptr, size_t sz, size_t nmemb, void *user)
{
    http_ctx_t       *ctx = user;
    size_t            total = sz * nmemb;
    esp_http_client_event_t evt = {0};

    evt.event_id    = HTTP_EVENT_ON_HEADER;
    evt.client      = ctx;
    evt.user_data   = ctx->user_data;
    evt.header_key  = NULL;
    evt.header_value = NULL;
    evt.data        = ptr;
    evt.data_len    = (int)total;
    if (ctx->event_handler) ctx->event_handler(&evt);

    return total;
}

/* fire ON_FINISH */
static void fire_on_finish(http_ctx_t *ctx)
{
    esp_http_client_event_t evt = {0};
    evt.event_id = HTTP_EVENT_ON_FINISH;
    evt.client   = ctx;
    evt.user_data = ctx->user_data;
    if (ctx->event_handler) ctx->event_handler(&evt);
}

/* ---- public API ---- */

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    if (!config) return NULL;

    http_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->easy = curl_easy_init();
    if (!ctx->easy) { free(ctx); return NULL; }

    ctx->url = config->url ? strdup(config->url) : NULL;
    ctx->event_handler = config->event_handler;
    ctx->user_data     = config->user_data;
    ctx->method        = config->method;

    if (ctx->url)
        curl_easy_setopt(ctx->easy, CURLOPT_URL, ctx->url);
    if (config->timeout_ms > 0) {
        curl_easy_setopt(ctx->easy, CURLOPT_TIMEOUT_MS, (long)config->timeout_ms);
    }
    curl_easy_setopt(ctx->easy, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(ctx->easy, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(ctx->easy, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(ctx->easy, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(ctx->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(ctx->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(ctx->easy, CURLOPT_SSL_VERIFYHOST, 0L);

    /* suppress curl's own output */
    curl_easy_setopt(ctx->easy, CURLOPT_NOPROGRESS, 1L);

    return ctx;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
                                     esp_http_client_method_t method)
{
    http_ctx_t *ctx = client;
    if (!ctx) return ESP_ERR_INVALID_ARG;
    ctx->method = method;

    switch (method) {
    case HTTP_METHOD_POST:   curl_easy_setopt(ctx->easy, CURLOPT_POST, 1L); break;
    case HTTP_METHOD_PUT:    curl_easy_setopt(ctx->easy, CURLOPT_UPLOAD, 1L); break;
    case HTTP_METHOD_DELETE: curl_easy_setopt(ctx->easy, CURLOPT_CUSTOMREQUEST, "DELETE"); break;
    case HTTP_METHOD_HEAD:   curl_easy_setopt(ctx->easy, CURLOPT_NOBODY, 1L); break;
    default: /* GET */       curl_easy_setopt(ctx->easy, CURLOPT_HTTPGET, 1L); break;
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value)
{
    http_ctx_t *ctx = client;
    if (!ctx || !key || !value) return ESP_ERR_INVALID_ARG;

    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "%s: %s", key, value);
    if (n < 0 || (size_t)n >= sizeof(buf)) return ESP_ERR_NO_MEM;

    struct curl_slist *h = curl_slist_append(ctx->headers, buf);
    if (!h) return ESP_ERR_NO_MEM;
    ctx->headers = h;
    curl_easy_setopt(ctx->easy, CURLOPT_HTTPHEADER, ctx->headers);

    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char *data, int len)
{
    http_ctx_t *ctx = client;
    if (!ctx || !data) return ESP_ERR_INVALID_ARG;

    free(ctx->post_data);
    ctx->post_data = malloc((size_t)len + 1);
    if (!ctx->post_data) return ESP_ERR_NO_MEM;
    memcpy(ctx->post_data, data, (size_t)len);
    ctx->post_data[len] = '\0';
    ctx->post_len = len;

    curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDS, ctx->post_data);
    curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDSIZE, (long)len);
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    http_ctx_t *ctx = client;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    ctx->connected_fired = false;
    CURLcode res = curl_easy_perform(ctx->easy);

    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(ctx->easy, CURLINFO_RESPONSE_CODE, &code);
        ctx->status_code = (int)code;
        fire_on_finish(ctx);
        return (ctx->status_code >= 200 && ctx->status_code < 300) ? ESP_OK : ESP_FAIL;
    }

    ESP_LOGE(TAG, "curl_easy_perform failed: %s", curl_easy_strerror(res));
    return ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    http_ctx_t *ctx = client;
    return ctx ? ctx->status_code : 500;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    http_ctx_t *ctx = client;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    esp_http_client_event_t evt = {0};
    evt.event_id = HTTP_EVENT_DISCONNECTED;
    evt.client   = ctx;
    evt.user_data = ctx->user_data;
    if (ctx->event_handler) ctx->event_handler(&evt);

    curl_easy_cleanup(ctx->easy);
    if (ctx->headers)  curl_slist_free_all(ctx->headers);
    free(ctx->post_data);
    free(ctx->url);
    free(ctx->write_buf);
    free(ctx->resp_buf);
    free(ctx);
    return ESP_OK;
}

/* ---- streaming API ---- */

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len)
{
    http_ctx_t *ctx = client;
    if (!ctx) return ESP_ERR_INVALID_ARG;

    ctx->streaming      = true;
    ctx->is_upload      = (write_len > 0);
    ctx->write_len      = 0;
    ctx->resp_read_pos  = 0;
    ctx->resp_len       = 0;
    ctx->connected_fired = false;

    if (ctx->is_upload) {
        free(ctx->write_buf);
        ctx->write_cap = (size_t)(write_len > 0 ? write_len : 4096);
        ctx->write_buf = malloc(ctx->write_cap);
        if (!ctx->write_buf) return ESP_ERR_NO_MEM;
        ctx->write_len = 0;
    } else {
        /* Download mode: perform request now, buffer response */
        free(ctx->resp_buf);
        ctx->resp_buf = NULL;
        ctx->resp_cap = 0;
        ctx->resp_len = 0;

        CURLcode res = curl_easy_perform(ctx->easy);
        if (res != CURLE_OK) {
            ESP_LOGE(TAG, "curl GET failed in open: %s", curl_easy_strerror(res));
            ctx->streaming = false;
            return ESP_FAIL;
        }
        long code = 0;
        curl_easy_getinfo(ctx->easy, CURLINFO_RESPONSE_CODE, &code);
        ctx->status_code = (int)code;
    }
    return ESP_OK;
}

int esp_http_client_write(esp_http_client_handle_t client,
                          const char *data, int len)
{
    http_ctx_t *ctx = client;
    if (!ctx || !ctx->streaming || !ctx->is_upload || !data || len <= 0)
        return -1;

    /* grow buffer if needed */
    if (ctx->write_len + (size_t)len > ctx->write_cap) {
        size_t new_cap = ctx->write_cap * 2;
        if (new_cap < ctx->write_len + (size_t)len)
            new_cap = ctx->write_len + (size_t)len;
        char *nb = realloc(ctx->write_buf, new_cap);
        if (!nb) return -1;
        ctx->write_buf = (char *)nb;
        ctx->write_cap = new_cap;
    }
    memcpy(ctx->write_buf + ctx->write_len, data, (size_t)len);
    ctx->write_len += (size_t)len;
    return len;
}

int esp_http_client_read(esp_http_client_handle_t client, char *buf, int len)
{
    http_ctx_t *ctx = client;
    if (!ctx || !ctx->streaming || !ctx->resp_buf || !buf || len <= 0)
        return -1;

    size_t remaining = ctx->resp_len - ctx->resp_read_pos;
    if (remaining == 0) return 0;  /* EOF */
    int to_copy = (int)(remaining < (size_t)len ? remaining : (size_t)len);
    memcpy(buf, ctx->resp_buf + ctx->resp_read_pos, (size_t)to_copy);
    ctx->resp_read_pos += (size_t)to_copy;
    return to_copy;
}

int esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    http_ctx_t *ctx = client;
    if (!ctx || !ctx->streaming) return -1;

    /* For uploads: the request hasn't been sent yet, so we can't get headers.
       For downloads: the request was sent in open(), headers already received. */
    if (ctx->is_upload) {
        /* Headers not yet available; will be after close().
           Return 0 to indicate no content-length known. */
        return 0;
    }

    /* parse Content-Length from response */
    curl_off_t cl = 0;
    CURLcode rc = curl_easy_getinfo(ctx->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    if (rc == CURLE_OK && cl > 0)
        return (int)cl;
    return -1;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    http_ctx_t *ctx = client;
    if (!ctx || !ctx->streaming) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = ESP_OK;

    if (ctx->is_upload && ctx->write_buf) {
        /* Perform the actual request with accumulated body */
        curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDS, ctx->write_buf);
        curl_easy_setopt(ctx->easy, CURLOPT_POSTFIELDSIZE, (long)ctx->write_len);

        CURLcode res = curl_easy_perform(ctx->easy);
        if (res == CURLE_OK) {
            long code = 0;
            curl_easy_getinfo(ctx->easy, CURLINFO_RESPONSE_CODE, &code);
            ctx->status_code = (int)code;
            fire_on_finish(ctx);
        } else {
            ESP_LOGE(TAG, "curl streaming POST failed: %s", curl_easy_strerror(res));
            ret = ESP_FAIL;
        }
    }

    ctx->streaming = false;
    return ret;
}
