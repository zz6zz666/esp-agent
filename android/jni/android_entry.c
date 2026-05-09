/*
 * android_entry.c — JNI entry points for the android port
 *
 * This file implements the JNI functions declared in
 * com.crushclaw.NativeBridge.java.  It starts the native agent
 * core (main_desktop entry equivalent) on a dedicated thread.
 */
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "display_hal.h"
#include "display_hal_android.h"
#include "display_arbiter.h"

static const char *TAG = "android_entry";

/* Thread handle for the native agent loop */
static pthread_t s_agent_thread;
static volatile bool s_agent_running = false;
volatile bool s_agent_should_stop = false;

/* Saved data dir path */
static char s_data_dir[512] = {0};

/* Forward declarations */
extern int desktop_main_android(const char *data_dir);
extern void claw_llm_http_arm_abort(volatile bool *flag);
extern void claw_llm_http_disarm_abort(void);

/* Agent thread entry point */
static void *agent_thread_entry(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Agent thread starting, data_dir=%s", s_data_dir);
    desktop_main_android(s_data_dir);
    ESP_LOGI(TAG, "Agent thread exited");
    s_agent_running = false;
    return NULL;
}

/* ---- JNI: nativeStart ---- */
JNIEXPORT void JNICALL
Java_com_crushclaw_NativeBridge_nativeStart(
    JNIEnv *env, jobject thiz,
    jstring dataDir, jobject serviceObj)
{
    (void)thiz;

    /* Save data dir path */
    const char *path = (*env)->GetStringUTFChars(env, dataDir, NULL);
    strncpy(s_data_dir, path, sizeof(s_data_dir) - 1);
    s_data_dir[sizeof(s_data_dir) - 1] = '\0';
    (*env)->ReleaseStringUTFChars(env, dataDir, path);

    /* Get JVM reference for callbacks */
    JavaVM *jvm;
    (*env)->GetJavaVM(env, &jvm);

    /* Create a global reference to the service object */
    jobject global_service = (*env)->NewGlobalRef(env, serviceObj);

    /* Initialize display JNI callbacks */
    display_android_init_jni(jvm, global_service);

    /* Start agent on dedicated thread */
    s_agent_running = true;
    s_agent_should_stop = false;

    int rc = pthread_create(&s_agent_thread, NULL, agent_thread_entry, NULL);

    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to create agent thread: %d", rc);
        s_agent_running = false;
    }

}

/* ---- JNI: nativeStop ---- */
JNIEXPORT void JNICALL
Java_com_crushclaw_NativeBridge_nativeStop(
    JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;

    ESP_LOGI(TAG, "Stopping agent...");

    /* Arm abort for any in-flight LLM HTTP call so it returns quickly */
    claw_llm_http_arm_abort(&s_agent_should_stop);
    s_agent_should_stop = true;

    /* Wake main loop if sleeping */
    display_hal_wake_main_loop();

    if (s_agent_running) {
        pthread_join(s_agent_thread, NULL);
    }

    claw_llm_http_disarm_abort();
    display_android_deinit_jni();
    ESP_LOGI(TAG, "Agent stopped");
}

/* ---- JNI: nativeNotifyFrameReady ---- */
JNIEXPORT void JNICALL
Java_com_crushclaw_NativeBridge_nativeNotifyFrameReady(
    JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;

    pthread_mutex_lock(&g_display_ctx.mutex);
    g_display_ctx.surface_ready = true;
    pthread_cond_broadcast(&g_display_ctx.frame_ready_cond);
    pthread_cond_broadcast(&g_display_ctx.surface_ready_cond);
    pthread_mutex_unlock(&g_display_ctx.mutex);
}

/* ---- JNI: nativeInjectTouch ---- */
JNIEXPORT void JNICALL
Java_com_crushclaw_NativeBridge_nativeInjectTouch(
    JNIEnv *env, jobject thiz,
    jint action, jint x, jint y)
{
    (void)env;
    (void)thiz;

    /* Forward to the touch bridge — see emote_stub.c:sim_touch_event_cb */
    extern void display_hal_inject_touch_android(int action, int x, int y);
    display_hal_inject_touch_android(action, x, y);
}

extern void display_hal_inject_touch_android(int action, int x, int y);

/* ---- JNI: nativeSwitchToEmote ---- */
JNIEXPORT void JNICALL
Java_com_crushclaw_NativeBridge_nativeSwitchToEmote(
    JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    ESP_LOGI(TAG, "nativeSwitchToEmote requested");

    extern display_arbiter_owner_t display_arbiter_get_owner(void);
    extern esp_err_t display_arbiter_release(display_arbiter_owner_t owner);

    while (display_arbiter_get_owner() == DISPLAY_ARBITER_OWNER_LUA) {
        esp_err_t err = display_arbiter_release(DISPLAY_ARBITER_OWNER_LUA);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "display_arbiter_release(LUA) returned %d", (int)err);
            break;
        }
    }
    ESP_LOGI(TAG, "nativeSwitchToEmote complete, owner=%d", (int)display_arbiter_get_owner());
}

/* ---- JNI: nativeReloadConfig ---- */
JNIEXPORT void JNICALL
Java_com_crushclaw_NativeBridge_nativeReloadConfig(
    JNIEnv *env, jobject thiz)
{
    (void)env;
    (void)thiz;
    ESP_LOGI(TAG, "Config reload requested (not yet implemented)");
}
