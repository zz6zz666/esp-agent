/*
 * esp_http_client_android.c — JNI-based esp_http_client for Android
 *
 * Implements the full esp_http_client API using Java's HttpURLConnection
 * via JNI. No libcurl/OpenSSL dependency.
 */
#include "esp_http_client.h"
#include "esp_log.h"
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_android";

/* ---- HTTP client context ---- */
typedef struct {
    char *url;
    char *body;
    int body_len;
    int method; /* 0=GET, 1=POST, etc. */
    int status_code;
    char *response_body;
    size_t response_len;
    bool performed;
    /* streaming */
    bool streaming;
    char *stream_buf;
    size_t stream_len;
    size_t stream_cap;
    size_t stream_pos;
    /* headers for building request */
    struct http_header {
        char key[128];
        char value[512];
        struct http_header *next;
    } *headers;
    /* event handler (for streaming response to caller buffer) */
    esp_http_client_event_handler_t event_handler;
    void *user_data;
} http_ctx_t;

static JavaVM *s_jvm = NULL;
static jclass s_http_client_class = NULL;

void esp_http_client_android_set_jvm(JavaVM *jvm) { s_jvm = jvm; }
void esp_http_client_android_set_class(jclass cls) { s_http_client_class = cls; }

static int call_java_http(const char *url, const char *method_str,
                           const char *body, int body_len,
                           struct http_header *headers,
                           int timeout_ms,
                           char **out_response, int *out_status)
{
    if (!s_jvm) { ESP_LOGE(TAG, "JVM not initialized"); return -1; }
    JNIEnv *env; bool need_detach = false;
    int ge = (*s_jvm)->GetEnv(s_jvm, (void**)&env, JNI_VERSION_1_6);
    if (ge == JNI_EDETACHED) { if ((*s_jvm)->AttachCurrentThread(s_jvm, &env, NULL) != JNI_OK) return -1; need_detach = true; }
    else if (ge != JNI_OK) return -1;

    jclass cls = s_http_client_class;
    if (!cls) { if (need_detach) (*s_jvm)->DetachCurrentThread(s_jvm); return -1; }
    jmethodID mid = (*env)->GetStaticMethodID(env, cls, "httpPostJson",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;I)[Ljava/lang/String;");

    jstring jUrl = (*env)->NewStringUTF(env, url);
    jstring jMethod = (*env)->NewStringUTF(env, method_str ? method_str : "GET");
    jstring jBody = (*env)->NewStringUTF(env, body ? body : "");
    jstring jApiKey = (*env)->NewStringUTF(env, "");
    jstring jAuthType = (*env)->NewStringUTF(env, "none");

    /* Build custom header array */
    int hcount = 0;
    for (struct http_header *h = headers; h; h = h->next) hcount++;
    jobjectArray jHeaders = (*env)->NewObjectArray(env, hcount, (*env)->FindClass(env, "java/lang/String"), NULL);
    int idx = 0;
    for (struct http_header *h = headers; h; h = h->next) {
        char buf[640]; snprintf(buf, sizeof(buf), "%s: %s", h->key, h->value);
        jstring hs = (*env)->NewStringUTF(env, buf);
        (*env)->SetObjectArrayElement(env, jHeaders, idx++, hs);
        (*env)->DeleteLocalRef(env, hs);
    }

    jint jTimeout = (jint)(timeout_ms > 0 ? timeout_ms : 30000);

    /* Log the HTTP call before making it */
    ESP_LOGI(TAG, "call_java_http: %s hdrs=%d", url ? url : "(null)", hcount);
    jobjectArray jResult = (jobjectArray)(*env)->CallStaticObjectMethod(env, cls, mid, jUrl, jMethod, jBody, jApiKey, jAuthType, jHeaders, jTimeout);

    /* Check for Java exception — unchecked exceptions abort ART on next JNI call */
    jthrowable exc = (*env)->ExceptionOccurred(env);
    if (exc) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jUrl); (*env)->DeleteLocalRef(env, jMethod);
        (*env)->DeleteLocalRef(env, jBody);
        (*env)->DeleteLocalRef(env, jApiKey); (*env)->DeleteLocalRef(env, jAuthType);
        (*env)->DeleteLocalRef(env, jHeaders);
        (*env)->DeleteLocalRef(env, exc);
        if (need_detach) (*s_jvm)->DetachCurrentThread(s_jvm);
        ESP_LOGE(TAG, "Java exception during HTTP POST to %s", url);
        return -1;
    }
    /* Cleanup (do NOT delete s_http_client_class — it's a global ref) */
    (*env)->DeleteLocalRef(env, jUrl); (*env)->DeleteLocalRef(env, jMethod);
    (*env)->DeleteLocalRef(env, jBody);
    (*env)->DeleteLocalRef(env, jApiKey); (*env)->DeleteLocalRef(env, jAuthType);
    (*env)->DeleteLocalRef(env, jHeaders);

    int ret = -1;
    if (jResult) {
        jstring jResp = (jstring)(*env)->GetObjectArrayElement(env, jResult, 0);
        jstring jStatus = (jstring)(*env)->GetObjectArrayElement(env, jResult, 1);
        const char *r = jResp ? (*env)->GetStringUTFChars(env, jResp, NULL) : NULL;
        const char *s = jStatus ? (*env)->GetStringUTFChars(env, jStatus, NULL) : NULL;
        if (out_status) *out_status = s ? atoi(s) : 0;
        if (out_response && r) *out_response = strdup(r);
        ret = 0;
        ESP_LOGI(TAG, "call_java_http OK: status=%d resp_len=%d", s ? atoi(s) : -1, r ? (int)strlen(r) : -1);
        if (r) (*env)->ReleaseStringUTFChars(env, jResp, r);
        if (s) (*env)->ReleaseStringUTFChars(env, jStatus, s);
        if (jResp) (*env)->DeleteLocalRef(env, jResp);
        if (jStatus) (*env)->DeleteLocalRef(env, jStatus);
        (*env)->DeleteLocalRef(env, jResult);
    } else {
        ESP_LOGE(TAG, "call_java_http: jResult is NULL for %s", url);
    }
    if (need_detach) (*s_jvm)->DetachCurrentThread(s_jvm);
    return ret;
}

/* ---- esp_http_client API implementation ---- */

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    if (!config) return NULL;
    http_ctx_t *ctx = calloc(1, sizeof(http_ctx_t));
    if (!ctx) return NULL;
    if (config->url) ctx->url = strdup(config->url);
    ctx->method = (int)config->method;
    ctx->event_handler = config->event_handler;
    ctx->user_data = config->user_data;
    return (esp_http_client_handle_t)ctx;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method)
{
    if (!client) return ESP_ERR_INVALID_ARG;
    ((http_ctx_t*)client)->method = (int)method;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value)
{
    if (!client || !key) return ESP_ERR_INVALID_ARG;
    http_ctx_t *ctx = (http_ctx_t*)client;
    struct http_header *h = calloc(1, sizeof(struct http_header));
    if (!h) return ESP_ERR_NO_MEM;
    strncpy(h->key, key, sizeof(h->key) - 1);
    if (value) strncpy(h->value, value, sizeof(h->value) - 1);
    h->next = ctx->headers;
    ctx->headers = h;
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len)
{
    if (!client) return ESP_ERR_INVALID_ARG;
    http_ctx_t *ctx = (http_ctx_t*)client;
    free(ctx->body);
    if (data && len > 0) {
        ctx->body = malloc((size_t)len + 1);
        memcpy(ctx->body, data, (size_t)len);
        ctx->body[len] = '\0';
        ctx->body_len = len;
    } else {
        ctx->body = NULL;
        ctx->body_len = 0;
    }
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    if (!client) return ESP_ERR_INVALID_ARG;
    http_ctx_t *ctx = (http_ctx_t*)client;
    free(ctx->response_body);
    ctx->response_body = NULL;
    ctx->response_len = 0;
    ctx->status_code = 0;

    ESP_LOGI(TAG, "HTTP perform: %s method=%d body_len=%d hdrs=%d",
             ctx->url ? ctx->url : "(null)", ctx->method, ctx->body_len,
             ctx->headers ? 1 : 0);

    int sc = 0;
    char *resp = NULL;
    int ret = call_java_http(ctx->url,
        ctx->method == 0 ? "GET" : "POST",
        ctx->body, ctx->body_len,
        ctx->headers, 30000, &resp, &sc);
    ctx->status_code = sc;
    if (resp) {
        ctx->response_body = resp;
        ctx->response_len = strlen(resp);
    }
    ctx->performed = true;

    /* Fire event handler with response data (for cap_im_qq etc. that use resp.buf) */
    if (ctx->event_handler && ctx->response_body) {
        esp_http_client_event_t evt = {0};
        evt.event_id = HTTP_EVENT_ON_DATA;
        evt.client = client;
        evt.user_data = ctx->user_data;
        evt.data = ctx->response_body;
        evt.data_len = (int)ctx->response_len;
        ctx->event_handler(&evt);
    }
    if (ctx->streaming && ctx->stream_buf) {
        /* For streaming: buffer the full response */
        free(ctx->stream_buf);
        ctx->stream_buf = ctx->response_body ? strdup(ctx->response_body) : NULL;
        ctx->stream_len = ctx->response_len;
        ctx->stream_cap = ctx->stream_len;
        ctx->stream_pos = 0;
    }
    return (ret == 0 && sc < 400) ? ESP_OK : ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    if (!client) return -1;
    return ((http_ctx_t*)client)->status_code;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    if (!client) return ESP_ERR_INVALID_ARG;
    http_ctx_t *ctx = (http_ctx_t*)client;
    free(ctx->url);
    free(ctx->body);
    free(ctx->response_body);
    free(ctx->stream_buf);
    while (ctx->headers) {
        struct http_header *next = ctx->headers->next;
        free(ctx->headers);
        ctx->headers = next;
    }
    free(ctx);
    return ESP_OK;
}

/* ---- Streaming API ---- */
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len)
{
    if (!client) return ESP_ERR_INVALID_ARG;
    http_ctx_t *ctx = (http_ctx_t*)client;
    ctx->streaming = true;
    (void)write_len;
    return ESP_OK;
}

int esp_http_client_write(esp_http_client_handle_t client, const char *data, int len)
{
    if (!client || !data) return -1;
    http_ctx_t *ctx = (http_ctx_t*)client;
    if (ctx->stream_pos + (size_t)len > ctx->stream_cap) {
        ctx->stream_cap = ctx->stream_pos + (size_t)len + 4096;
        ctx->stream_buf = realloc(ctx->stream_buf, ctx->stream_cap);
    }
    memcpy(ctx->stream_buf + ctx->stream_pos, data, (size_t)len);
    ctx->stream_pos += (size_t)len;
    if (ctx->stream_len < ctx->stream_pos) ctx->stream_len = ctx->stream_pos;
    return len;
}

int esp_http_client_read(esp_http_client_handle_t client, char *buf, int len)
{
    if (!client || !buf) return -1;
    http_ctx_t *ctx = (http_ctx_t*)client;
    if (!ctx->response_body) return 0;
    size_t remaining = ctx->response_len - ctx->stream_pos;
    int to_read = (remaining < (size_t)len) ? (int)remaining : len;
    if (to_read <= 0) return 0;
    memcpy(buf, ctx->response_body + ctx->stream_pos, (size_t)to_read);
    ctx->stream_pos += (size_t)to_read;
    return to_read;
}

int esp_http_client_fetch_headers(esp_http_client_handle_t client)
{
    (void)client;
    return 0;
}

esp_err_t esp_http_client_close(esp_http_client_handle_t client)
{
    if (!client) return ESP_ERR_INVALID_ARG;
    http_ctx_t *ctx = (http_ctx_t*)client;
    ctx->streaming = false;
    ctx->stream_pos = 0;
    return ESP_OK;
}
