/*
 * http_curl.c — libcurl replacement for esp_http_client in desktop simulator
 *
 * Implements the claw_llm_http_transport.h interface using libcurl.
 * Supports HTTP POST with JSON body, custom headers, auth, TLS, and abort.
 */
#include <assert.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "llm/claw_llm_http_transport.h"
#include "llm/claw_llm_types.h"

static const char *TAG = "llm_http";

/* ---- Abort support ---- */
static volatile bool *s_abort_flag = NULL;
static pthread_t      s_abort_owner = 0;

void claw_llm_http_arm_abort(volatile bool *flag)
{
    pthread_t self = pthread_self();
    assert(s_abort_flag == NULL || s_abort_owner == self);
    s_abort_flag = flag;
    s_abort_owner = self;
}

void claw_llm_http_disarm_abort(void)
{
    if (s_abort_owner == pthread_self()) {
        s_abort_flag = NULL;
        s_abort_owner = 0;
    }
}

static inline bool abort_requested(void)
{
    return s_abort_flag &&
           s_abort_owner == pthread_self() &&
           *s_abort_flag;
}

/* ---- libcurl write callback ---- */

struct response_buffer {
    char *data;
    size_t len;
    size_t cap;
};

static esp_err_t rb_init(struct response_buffer *rb)
{
    rb->data = calloc(1, 4096);
    if (!rb->data) return ESP_ERR_NO_MEM;
    rb->cap = 4096;
    rb->len = 0;
    return ESP_OK;
}

static esp_err_t rb_append(struct response_buffer *rb, const char *data, size_t len)
{
    while (rb->len + len + 1 > rb->cap) {
        rb->cap *= 2;
    }
    char *grown = realloc(rb->data, rb->cap);
    if (!grown) return ESP_ERR_NO_MEM;
    rb->data = grown;
    memcpy(rb->data + rb->len, data, len);
    rb->len += len;
    rb->data[rb->len] = '\0';
    return ESP_OK;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct response_buffer *rb = (struct response_buffer *)userdata;
    size_t total = size * nmemb;

    if (abort_requested()) return 0; /* signals error to libcurl */

    if (rb_append(rb, ptr, total) != ESP_OK) return 0;
    return total;
}

/* Abort check during transfer — called periodically by libcurl */
static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow)
{
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return abort_requested() ? 1 : 0;
}

/* ---- Headers ---- */

struct curl_slist *build_headers(const claw_llm_http_json_request_t *req)
{
    struct curl_slist *headers = NULL;
    char auth_value[4096];

    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (req->api_key && req->api_key[0]) {
        const char *kind = req->auth_type ? req->auth_type : "bearer";
        if (strcmp(kind, "none") != 0) {
            if (strcmp(kind, "api-key") == 0) {
                snprintf(auth_value, sizeof(auth_value), "X-API-Key: %s", req->api_key);
            } else {
                snprintf(auth_value, sizeof(auth_value), "Authorization: Bearer %s", req->api_key);
            }
            headers = curl_slist_append(headers, auth_value);
        }
    }

    for (size_t i = 0; i < req->header_count; i++) {
        if (req->headers[i].name && req->headers[i].name[0] && req->headers[i].value) {
            char buf[4096];
            snprintf(buf, sizeof(buf), "%s: %s", req->headers[i].name, req->headers[i].value);
            headers = curl_slist_append(headers, buf);
        }
    }

    return headers;
}

/* ---- Error parsing ---- */

static char *parse_error_body(const char *body, int status)
{
    if (!body || !body[0]) {
        char *msg = malloc(64);
        if (msg) snprintf(msg, 64, "HTTP %d", status);
        return msg;
    }

    /* Minimal JSON error extraction without cJSON dependency at this layer.
     * If cJSON is available, prefer that. We include a simple fallback. */
    const char *msg_start = strstr(body, "\"message\"");
    if (msg_start) {
        msg_start = strchr(msg_start, ':');
        if (msg_start) {
            msg_start++; /* skip ':' */
            while (*msg_start == ' ' || *msg_start == '"') msg_start++;
            const char *msg_end = strchr(msg_start, '"');
            if (msg_end && msg_end > msg_start) {
                size_t len = (size_t)(msg_end - msg_start);
                char *result = malloc(len + 64);
                if (result) snprintf(result, len + 64, "HTTP %d: %.*s", status, (int)len, msg_start);
                return result;
            }
        }
    }

    size_t body_len = strlen(body);
    size_t snippet_len = body_len > 160 ? 160 : body_len;
    char *result = malloc(snippet_len + 64);
    if (result) snprintf(result, snippet_len + 64, "HTTP %d: %.*s", status, (int)snippet_len, body);
    return result;
}

/* ---- Main POST ---- */

esp_err_t claw_llm_http_post_json(const claw_llm_http_json_request_t *request,
                                   claw_llm_http_response_t *out_response,
                                   char **out_error_message)
{
    struct response_buffer rb = {0};
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    CURLcode rc;
    long status_code = 0;
    esp_err_t err;

    if (out_response) memset(out_response, 0, sizeof(*out_response));
    if (out_error_message) *out_error_message = NULL;
    if (!request || !request->url || !request->body || !out_response || !out_error_message) {
        return ESP_ERR_INVALID_ARG;
    }

    err = rb_init(&rb);
    if (err != ESP_OK) {
        *out_error_message = strdup("Out of memory allocating HTTP buffer");
        return err;
    }

    curl = curl_easy_init();
    if (!curl) {
        *out_error_message = strdup("Failed to initialize libcurl");
        free(rb.data);
        return ESP_FAIL;
    }

    headers = build_headers(request);

    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(request->body));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rb);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request->timeout_ms ? request->timeout_ms : 30000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "esp-claw-desktop/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    ESP_LOGD(TAG, "POST %s", request->url);
    rc = curl_easy_perform(curl);

    if (rc != CURLE_OK) {
        if (abort_requested()) {
            *out_error_message = strdup("HTTP request aborted by caller");
            err = ESP_ERR_INVALID_STATE;
        } else {
            size_t needed = strlen(curl_easy_strerror(rc)) + 64;
            *out_error_message = malloc(needed);
            if (*out_error_message) {
                snprintf(*out_error_message, needed, "HTTP request failed: %s", curl_easy_strerror(rc));
            }
            err = ESP_FAIL;
        }
        goto cleanup;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    ESP_LOGD(TAG, "HTTP status=%ld", status_code);

    if (status_code != 200) {
        err = ESP_FAIL;
        *out_error_message = parse_error_body(rb.data, (int)status_code);
        ESP_LOGE(TAG, "LLM error: %s", *out_error_message ? *out_error_message : "(null)");
        goto cleanup;
    }

    out_response->status_code = (int)status_code;
    out_response->body = rb.data;
    rb.data = NULL;
    err = ESP_OK;

cleanup:
    curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
    free(rb.data);
    return err;
}

void claw_llm_http_response_free(claw_llm_http_response_t *response)
{
    if (!response) return;
    free(response->body);
    memset(response, 0, sizeof(*response));
}
