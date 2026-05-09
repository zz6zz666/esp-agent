/*
 * http_transport_android.c — JNI-based HTTP POST for Android
 *
 * Implements claw_llm_http_post_json and friends by calling
 * com.crushclaw.HttpTransport.httpPostJson() via JNI.
 * Replaces http_curl.c / libcurl on Android.
 */
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

/* The LLM transport interface we need to implement */
#include "llm/claw_llm_types.h"
#include "llm/claw_llm_http_transport.h"

static const char *TAG = "http_transport";

/* ---- Global JVM reference (set from android_entry.c) ---- */
static JavaVM *s_jvm = NULL;

/* ---- Cached HttpTransport class (global ref, set on main thread) ---- */
static jclass s_http_transport_class = NULL;

void http_transport_set_class(jclass cls)
{
    s_http_transport_class = cls;
}

/* ---- Abort flag support (thread-safe) ---- */
static pthread_mutex_t s_abort_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool *s_abort_flag = NULL;
static pthread_t s_abort_owner;

void claw_llm_http_arm_abort(volatile bool *flag)
{
    pthread_mutex_lock(&s_abort_mutex);
    s_abort_flag = flag;
    s_abort_owner = pthread_self();
    pthread_mutex_unlock(&s_abort_mutex);
}

void claw_llm_http_disarm_abort(void)
{
    pthread_mutex_lock(&s_abort_mutex);
    s_abort_flag = NULL;
    pthread_mutex_unlock(&s_abort_mutex);
}

static bool is_aborted(void)
{
    bool aborted = false;
    pthread_mutex_lock(&s_abort_mutex);
    if (s_abort_flag && *s_abort_flag) aborted = true;
    pthread_mutex_unlock(&s_abort_mutex);
    return aborted;
}

/* Called from android_entry.c to set JVM ref */
void http_transport_set_jvm(JavaVM *jvm)
{
    s_jvm = jvm;
}

/* ================================================================
 * claw_llm_http_post_json — JNI implementation
 * ================================================================ */

esp_err_t claw_llm_http_post_json(const claw_llm_http_json_request_t *request,
                                  claw_llm_http_response_t *out_response,
                                  char **out_error_message)
{
    if (!request || !out_response) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_jvm) {
        if (out_error_message) *out_error_message = strdup("JVM not initialized");
        return ESP_FAIL;
    }

    if (is_aborted()) {
        if (out_error_message) *out_error_message = strdup("Aborted");
        return ESP_FAIL;
    }

    JNIEnv *env;
    bool need_detach = false;
    int get_env = (*s_jvm)->GetEnv(s_jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*s_jvm)->AttachCurrentThread(s_jvm, &env, NULL) != JNI_OK) {
            if (out_error_message) *out_error_message = strdup("Failed to attach JNI thread");
            return ESP_FAIL;
        }
        need_detach = true;
    } else if (get_env != JNI_OK) {
        if (out_error_message) *out_error_message = strdup("JNI environment unavailable");
        return ESP_FAIL;
    }

    esp_err_t result = ESP_FAIL;
    *out_response = (claw_llm_http_response_t){0};

    /* Find HttpTransport class */
    jclass cls = s_http_transport_class;
    if (!cls) {
        if (out_error_message) *out_error_message = strdup("Java class HttpTransport not found");
        if (need_detach) (*s_jvm)->DetachCurrentThread(s_jvm);
        return ESP_FAIL;
    }

    /* Note: cls is a global ref — do NOT DeleteLocalRef it. */

    if (is_aborted()) {
        if (need_detach) (*s_jvm)->DetachCurrentThread(s_jvm);
        if (out_error_message) *out_error_message = strdup("Aborted");
        return ESP_FAIL;
    }

    /* Find the static httpPostJson method */
    jmethodID mid = (*env)->GetStaticMethodID(env, cls, "httpPostJson",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;I)[Ljava/lang/String;");
    if (!mid) {
        if (out_error_message) *out_error_message = strdup("Method httpPostJson not found");
        if (need_detach) (*s_jvm)->DetachCurrentThread(s_jvm);
        return ESP_FAIL;
    }

    /* Convert C strings to Java strings */
    jstring jUrl = (*env)->NewStringUTF(env, request->url ? request->url : "");
    jstring jBody = (*env)->NewStringUTF(env, request->body ? request->body : "");
    jstring jApiKey = (*env)->NewStringUTF(env, request->api_key ? request->api_key : "");
    jstring jAuthType = (*env)->NewStringUTF(env, request->auth_type ? request->auth_type : "");

    /* Build String array for custom headers */
    jobjectArray jHeaders = NULL;
    if (request->headers && request->header_count > 0) {
        jHeaders = (*env)->NewObjectArray(env, (jsize)request->header_count,
            (*env)->FindClass(env, "java/lang/String"), NULL);
        for (size_t i = 0; i < request->header_count; i++) {
            char header_buf[512];
            snprintf(header_buf, sizeof(header_buf), "%s: %s",
                request->headers[i].name ? request->headers[i].name : "",
                request->headers[i].value ? request->headers[i].value : "");
            jstring h = (*env)->NewStringUTF(env, header_buf);
            (*env)->SetObjectArrayElement(env, jHeaders, (jsize)i, h);
            (*env)->DeleteLocalRef(env, h);
        }
    }

    jint jTimeout = (jint)(request->timeout_ms > 0 ? request->timeout_ms : 30000);

    if (is_aborted()) {
        goto cleanup;
    }

    /* Call Java HttpTransport.httpPostJson() */
    jobjectArray jResult = (jobjectArray)(*env)->CallStaticObjectMethod(env, cls, mid,
        jUrl, jBody, jApiKey, jAuthType, jHeaders, jTimeout);

    /* Check for Java exception — unchecked exceptions abort ART on next JNI call */
    jthrowable exc = (*env)->ExceptionOccurred(env);
    if (exc) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, exc);
        if (out_error_message) *out_error_message = strdup("Java exception during HTTP POST");
        goto cleanup;
    }

    if (!jResult) {
        if (out_error_message) *out_error_message = strdup("Java call returned null");
        goto cleanup;
    }

    /* Extract result strings: [body, statusCode, errorMessage] */
    jstring jRespBody = (jstring)(*env)->GetObjectArrayElement(env, jResult, 0);
    jstring jStatusCode = (jstring)(*env)->GetObjectArrayElement(env, jResult, 1);
    jstring jErrorMsg = (jstring)(*env)->GetObjectArrayElement(env, jResult, 2);

    const char *resp_body = jRespBody ? (*env)->GetStringUTFChars(env, jRespBody, NULL) : "";
    const char *status_code = jStatusCode ? (*env)->GetStringUTFChars(env, jStatusCode, NULL) : "0";
    const char *err_msg = jErrorMsg ? (*env)->GetStringUTFChars(env, jErrorMsg, NULL) : "";

    out_response->body = resp_body[0] ? strdup(resp_body) : NULL;
    out_response->status_code = atoi(status_code);

    if (err_msg[0]) {
        if (out_error_message) *out_error_message = strdup(err_msg);
        result = ESP_FAIL;
    } else {
        result = ESP_OK;
    }

    /* Release Java string chars */
    if (jRespBody && resp_body) (*env)->ReleaseStringUTFChars(env, jRespBody, resp_body);
    if (jStatusCode && status_code) (*env)->ReleaseStringUTFChars(env, jStatusCode, status_code);
    if (jErrorMsg && err_msg) (*env)->ReleaseStringUTFChars(env, jErrorMsg, err_msg);

    (*env)->DeleteLocalRef(env, jRespBody);
    (*env)->DeleteLocalRef(env, jStatusCode);
    (*env)->DeleteLocalRef(env, jErrorMsg);
    (*env)->DeleteLocalRef(env, jResult);

cleanup:
    (*env)->DeleteLocalRef(env, jUrl);
    (*env)->DeleteLocalRef(env, jBody);
    (*env)->DeleteLocalRef(env, jApiKey);
    (*env)->DeleteLocalRef(env, jAuthType);
    if (jHeaders) (*env)->DeleteLocalRef(env, jHeaders);

    if (need_detach) {
        (*s_jvm)->DetachCurrentThread(s_jvm);
    }

    return result;
}

void claw_llm_http_response_free(claw_llm_http_response_t *response)
{
    if (response) {
        free(response->body);
        response->body = NULL;
        response->status_code = 0;
    }
}
