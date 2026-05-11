/*
 * display_hal_android.c — Android implementation of display_hal.h
 *
 * Replaces display_sdl2.c for the Android platform.
 *
 * Architecture:
 *   - All rendering goes into a single RGB565 pixel buffer (uint16_t*)
 *   - Window lifecycle (create/destroy/hide/show/switch) is delegated
 *     to Java via JNI callbacks
 *   - Frame presentation: C renders → signals Java → Java copies buffer
 *     into an Android Bitmap → displays in the floating ImageView
 *   - Mode switch (emote ↔ Lua) follows the same pattern as SDL2:
 *     destroy old window → create new window at the target size
 *
 * Pixel rendering is 1:1 — no system scaling applied.
 * This matches the Windows desktop behavior exactly.
 */
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "display_hal.h"
#include "display_hal_android.h"
#include "display_arbiter.h"
#include "esp_lcd_touch.h"
#include "font_android.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

/* JPEG decoder (stb_image single-header) */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"

static const char *TAG = "display_android";

/* ---- Built-in 8x16 VGA bitmap font (first 128 chars, ASCII) ---- */
#include "builtin_font_8x16.h"

/* TrueType font system active (initialized once at startup) */
bool g_font_android_ok = false;

/* ---- Global context ---- */
display_android_ctx_t g_display_ctx = {0};

static pthread_t s_main_thread;
static pthread_mutex_t s_present_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_present_cond  = PTHREAD_COND_INITIALIZER;
static volatile bool g_present_pending = false;

/* ---- Platform stubs (avoid link errors for desktop-specific functions) ---- */

void display_hal_set_initial_position(int x, int y) { (void)x; (void)y; }
void display_hal_set_always_hide(bool hide) { g_display_ctx.always_hide = hide; }
bool display_hal_is_active(void) { return g_display_ctx.pixels != NULL; }
bool display_hal_is_lua_mode(void) { return g_display_ctx.lua_mode; }
bool display_hal_consume_lua_switch_notification(void) { return false; }
void display_hal_save_window_position(void) {}
bool display_hal_recreate_emote(void) { return false; }
void display_hal_show_window(void) { display_android_notify_display_enable(true); }
void display_hal_hide_window(void) { display_android_notify_display_enable(false); }
bool display_hal_title_minimize_hit(void) { return false; }
/* ---- Toast text (emote overlay, matches desktop draw_toast_overlay) ---- */
static char s_toast_text[96];

void display_hal_set_toast_text(const char *text)
{
    if (text && text[0]) {
        strncpy(s_toast_text, text, sizeof(s_toast_text) - 1);
        s_toast_text[sizeof(s_toast_text) - 1] = '\0';
    } else {
        s_toast_text[0] = '\0';
    }
}

static void draw_toast_overlay(void);  /* defined near display_hal_present */
void *display_hal_get_native_window(void) { return NULL; }
extern void emote_handle_tap(void);

void display_hal_inject_touch_android(int action, int x, int y)
{
    if (action == 0) {
        esp_lcd_touch_feed_sdl((int16_t)x, (int16_t)y, true);
    } else if (action == 1) {
        esp_lcd_touch_feed_sdl(0, 0, false);
        emote_handle_tap();
    }
}

bool display_hal_is_always_hide(void) { return g_display_ctx.always_hide; }
bool display_hal_is_emote_visible(void) { return g_display_ctx.emote_visible; }
void display_hal_set_emote_visible(bool vis) {
    g_display_ctx.emote_visible = vis;
    if (!vis) display_android_notify_display_enable(false);
    else display_android_notify_display_enable(true);
}

/* ---- External: signal Lua frame render ---- */
extern void display_hal_mark_frame_ready(void);

/* ================================================================
 * JNI callback helpers
 * ================================================================ */

esp_err_t display_android_init_jni(JavaVM *jvm, jobject service_obj)
{
    JNIEnv *env;
    bool need_detach = false;

    if ((*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) {
            ESP_LOGE(TAG, "Failed to attach JNI thread");
            return ESP_FAIL;
        }
        need_detach = true;
    }

    g_display_ctx.jvm = jvm;
    g_display_ctx.service_obj = (*env)->NewGlobalRef(env, service_obj);

    jclass cls = (*env)->GetObjectClass(env, service_obj);
    g_display_ctx.mid_on_display_create = (*env)->GetMethodID(env, cls,
        "onDisplayCreate", "(II)V");
    g_display_ctx.mid_on_display_destroy = (*env)->GetMethodID(env, cls,
        "onDisplayDestroy", "()V");
    g_display_ctx.mid_on_owner_changed = (*env)->GetMethodID(env, cls,
        "onDisplayOwnerChanged", "(III)V");
    g_display_ctx.mid_on_frame_ready = (*env)->GetMethodID(env, cls,
        "onFrameReady", "([BII)V");
    g_display_ctx.mid_on_display_enable = (*env)->GetMethodID(env, cls,
        "onDisplayEnable", "(Z)V");
    g_display_ctx.mid_on_restart_requested = (*env)->GetMethodID(env, cls,
        "onRestartRequested", "()V");

    (*env)->DeleteLocalRef(env, cls);

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
    return ESP_OK;
}

void display_android_deinit_jni(void)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm) return;

    if ((*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        if (g_display_ctx.service_obj) {
            (*env)->DeleteGlobalRef(env, g_display_ctx.service_obj);
            g_display_ctx.service_obj = NULL;
        }
    }
    g_display_ctx.jvm = NULL;
}

static void call_java_void_method_int_int(jmethodID mid, int a, int b)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm || !g_display_ctx.service_obj) return;

    bool need_detach = false;
    int get_env = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return;
        need_detach = true;
    } else if (get_env != JNI_OK) {
        return;
    }

    (*env)->CallVoidMethod(env, g_display_ctx.service_obj, mid, a, b);

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
}

static void call_java_void_method_int_int_int(jmethodID mid, int a, int b, int c)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm || !g_display_ctx.service_obj) return;

    bool need_detach = false;
    int get_env = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return;
        need_detach = true;
    } else if (get_env != JNI_OK) {
        return;
    }

    (*env)->CallVoidMethod(env, g_display_ctx.service_obj, mid, a, b, c);

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
}

static void call_java_frame_ready(const uint16_t *pixels, int w, int h)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm || !g_display_ctx.service_obj || !pixels) return;
    if (!g_display_ctx.mid_on_frame_ready) return;

    bool need_detach = false;
    int get_env = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return;
        need_detach = true;
    } else if (get_env != JNI_OK) {
        return;
    }

    int size = w * h * 2;
    jbyteArray arr = (*env)->NewByteArray(env, size);
    if (arr) {
        (*env)->SetByteArrayRegion(env, arr, 0, size, (const jbyte*)pixels);
        (*env)->CallVoidMethod(env, g_display_ctx.service_obj,
            g_display_ctx.mid_on_frame_ready, arr, w, h);
        (*env)->DeleteLocalRef(env, arr);
    }

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
}

void display_android_notify_frame_ready(void)
{
    call_java_frame_ready(g_display_ctx.pixels,
        g_display_ctx.width, g_display_ctx.height);
}

void display_android_notify_create(int lua_w, int lua_h)
{
    call_java_void_method_int_int(
        g_display_ctx.mid_on_display_create, lua_w, lua_h);
}

void display_android_notify_destroy(void)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm || !g_display_ctx.service_obj) return;

    bool need_detach = false;
    int get_env = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return;
        need_detach = true;
    } else if (get_env != JNI_OK) {
        return;
    }

    (*env)->CallVoidMethod(env, g_display_ctx.service_obj,
        g_display_ctx.mid_on_display_destroy);

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
}

void display_android_notify_owner_change(int owner_mode, int w, int h)
{
    call_java_void_method_int_int_int(
        g_display_ctx.mid_on_owner_changed, owner_mode, w, h);
}

void display_android_notify_display_enable(bool enable)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm || !g_display_ctx.service_obj) return;

    bool need_detach = false;
    int get_env = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return;
        need_detach = true;
    } else if (get_env != JNI_OK) {
        return;
    }

    (*env)->CallVoidMethod(env, g_display_ctx.service_obj,
        g_display_ctx.mid_on_display_enable, (jboolean)enable);

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
}

void display_android_request_restart(void)
{
    JNIEnv *env;
    JavaVM *jvm = g_display_ctx.jvm;
    if (!jvm || !g_display_ctx.service_obj) return;

    bool need_detach = false;
    int get_env = (*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6);
    if (get_env == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return;
        need_detach = true;
    } else if (get_env != JNI_OK) {
        return;
    }

    ESP_LOGI(TAG, "Requesting process restart via JNI callback");
    (*env)->CallVoidMethod(env, g_display_ctx.service_obj,
        g_display_ctx.mid_on_restart_requested);

    if (need_detach) {
        (*jvm)->DetachCurrentThread(jvm);
    }
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

esp_err_t display_hal_create(esp_lcd_panel_handle_t panel_handle,
                             esp_lcd_panel_io_handle_t io_handle,
                             display_hal_panel_if_t panel_if,
                             int lcd_width, int lcd_height)
{
    (void)panel_handle; (void)io_handle; (void)panel_if;

    int lcd_w = lcd_width  > 0 ? lcd_width  : 480;
    int lcd_h = lcd_height > 0 ? lcd_height : 480;

    /* Worker thread path (display.init() in Lua):
     * Switch from emote to Lua mode — free emote buffers, allocate
     * Lua-sized ones, notify Java, block until window is ready. */
    if (g_display_ctx.pixels) {
        ESP_LOGI(TAG, "display_hal_create (worker): switch to Lua %dx%d", lcd_w, lcd_h);

        if (g_display_ctx.pixels)      { free(g_display_ctx.pixels);      g_display_ctx.pixels = NULL; }
        if (g_display_ctx.pixels_draw) { free(g_display_ctx.pixels_draw); g_display_ctx.pixels_draw = NULL; }

        g_display_ctx.lcd_width  = lcd_w;
        g_display_ctx.lcd_height = lcd_h;
        g_display_ctx.lua_width  = lcd_w;
        g_display_ctx.lua_height = lcd_h;
        g_display_ctx.width  = lcd_w;
        g_display_ctx.height = lcd_h;

        g_display_ctx.pixels      = calloc(1, (size_t)lcd_w * lcd_h * 2);
        g_display_ctx.pixels_draw = calloc(1, (size_t)lcd_w * lcd_h * 2);
        g_display_ctx.lua_mode = true;
        g_display_ctx.emote_visible = false;
        g_display_ctx.surface_ready = false;

        display_android_notify_owner_change(1, lcd_w, lcd_h);

        {
            struct timeval start, now;
            gettimeofday(&start, NULL);
            pthread_mutex_lock(&g_display_ctx.mutex);
            while (!g_display_ctx.surface_ready) {
                struct timespec ts;
                gettimeofday(&now, NULL);
                ts.tv_sec = now.tv_sec + 1;
                ts.tv_nsec = now.tv_usec * 1000;
                pthread_cond_timedwait(&g_display_ctx.surface_ready_cond,
                                       &g_display_ctx.mutex, &ts);
                gettimeofday(&now, NULL);
                if (now.tv_sec - start.tv_sec >= 3) {
                    ESP_LOGW(TAG, "Timed out waiting for Java window (Lua)");
                    break;
                }
            }
            pthread_mutex_unlock(&g_display_ctx.mutex);
        }
        ESP_LOGI(TAG, "Lua mode switch complete: %dx%d", lcd_w, lcd_h);
        return ESP_OK;
    }

    /* Main thread path (first-time init from desktop_main) */
    ESP_LOGI(TAG, "display_hal_create (main): lcd=%dx%d", lcd_w, lcd_h);

    pthread_mutex_init(&g_display_ctx.mutex, NULL);
    pthread_cond_init(&g_display_ctx.frame_ready_cond, NULL);
    pthread_cond_init(&g_display_ctx.surface_ready_cond, NULL);

    g_display_ctx.lcd_width  = lcd_w;
    g_display_ctx.lcd_height = lcd_h;
    g_display_ctx.emu_width   = 320;
    g_display_ctx.emu_height  = 240;
    g_display_ctx.lua_width   = lcd_w;
    g_display_ctx.lua_height  = lcd_h;
    g_display_ctx.lua_mode     = false;
    g_display_ctx.emote_visible = true;
    g_display_ctx.emote_was_visible = true;
    g_display_ctx.frame_active = false;
    g_display_ctx.clip_enabled = false;

    g_display_ctx.width  = g_display_ctx.emu_width;
    g_display_ctx.height = g_display_ctx.emu_height;
    g_display_ctx.pixels = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);
    if (!g_display_ctx.pixels) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer");
        return ESP_ERR_NO_MEM;
    }

    display_android_notify_create(lcd_w, lcd_h);

    s_main_thread = pthread_self();
    ESP_LOGI(TAG, "Display created: Lua=%dx%d Emote=%dx%d",
             lcd_w, lcd_h, g_display_ctx.emu_width, g_display_ctx.emu_height);
    return ESP_OK;
}

esp_err_t display_hal_destroy(void)
{
    ESP_LOGI(TAG, "display_hal_destroy (lua_mode=%d)", (int)g_display_ctx.lua_mode);

    if (g_display_ctx.lua_mode) {
        /* Lua → Emote switch-back: free Lua buffers, allocate emote buffer,
         * notify Java, block until window is ready. */
        g_display_ctx.lua_mode = false;

        if (g_display_ctx.pixels)      { free(g_display_ctx.pixels);      g_display_ctx.pixels = NULL; }
        if (g_display_ctx.pixels_draw) { free(g_display_ctx.pixels_draw); g_display_ctx.pixels_draw = NULL; }

        if (g_display_ctx.emote_was_visible) {
            g_display_ctx.emote_visible = true;
            g_display_ctx.width  = g_display_ctx.emu_width;
            g_display_ctx.height = g_display_ctx.emu_height;
            g_display_ctx.pixels = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);
            g_display_ctx.surface_ready = false;

            display_android_notify_owner_change(2, g_display_ctx.emu_width, g_display_ctx.emu_height);

            {
                struct timeval start, now;
                gettimeofday(&start, NULL);
                pthread_mutex_lock(&g_display_ctx.mutex);
                while (!g_display_ctx.surface_ready) {
                    struct timespec ts;
                    gettimeofday(&now, NULL);
                    ts.tv_sec = now.tv_sec + 1;
                    ts.tv_nsec = now.tv_usec * 1000;
                    pthread_cond_timedwait(&g_display_ctx.surface_ready_cond,
                                           &g_display_ctx.mutex, &ts);
                    gettimeofday(&now, NULL);
                    if (now.tv_sec - start.tv_sec >= 3) {
                        ESP_LOGW(TAG, "Timed out waiting for Java window (emote)");
                        break;
                    }
                }
                pthread_mutex_unlock(&g_display_ctx.mutex);
            }
        } else {
            display_android_notify_owner_change(0, 0, 0);
        }
        g_display_ctx.frame_active = false;
        return ESP_OK;
    }

    /* App shutdown path */
    if (g_display_ctx.pixels) {
        free(g_display_ctx.pixels);
        g_display_ctx.pixels = NULL;
    }
    if (g_display_ctx.pixels_draw) {
        free(g_display_ctx.pixels_draw);
        g_display_ctx.pixels_draw = NULL;
    }

    g_display_ctx.width = 0;
    g_display_ctx.height = 0;
    g_display_ctx.frame_active = false;
    g_display_ctx.lua_mode = false;
    g_display_ctx.emote_visible = false;

    display_android_notify_destroy();
    return ESP_OK;
}

/* ================================================================
 * Geometry
 * ================================================================ */

int display_hal_width(void)  { return g_display_ctx.lcd_width; }
int display_hal_height(void) { return g_display_ctx.lcd_height; }

/* ================================================================
 * Frame control
 * ================================================================ */

/* Return the buffer drawing primitives should write to.
 * Lua mode uses a separate draw buffer to avoid tearing;
 * emote mode draws directly to the display buffer. */
static inline uint16_t *draw_pixels(void)
{
    if (g_display_ctx.lua_mode && g_display_ctx.pixels_draw)
        return g_display_ctx.pixels_draw;
    return g_display_ctx.pixels;
}

esp_err_t display_hal_begin_frame(bool clear, uint16_t color565)
{
    /* Fallback switch to Lua mode — matches display_sdl2.c begin_frame
     * (lines 936-958).  Only fires on the worker thread (non-main).
     * Boot animation runs on the main thread with arbiter=EMOTE and
     * must NOT trigger a switch. */
    if (!g_display_ctx.lua_mode && !pthread_equal(pthread_self(), s_main_thread)) {
        ESP_LOGI(TAG, "begin_frame fallback: switching to Lua %dx%d",
                 g_display_ctx.lua_width, g_display_ctx.lua_height);

        display_arbiter_acquire(DISPLAY_ARBITER_OWNER_LUA);

        if (g_display_ctx.pixels)      { free(g_display_ctx.pixels);      g_display_ctx.pixels = NULL; }
        if (g_display_ctx.pixels_draw) { free(g_display_ctx.pixels_draw); g_display_ctx.pixels_draw = NULL; }

        g_display_ctx.width  = g_display_ctx.lua_width;
        g_display_ctx.height = g_display_ctx.lua_height;
        g_display_ctx.pixels      = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);
        g_display_ctx.pixels_draw = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);

        g_display_ctx.lua_mode = true;
        g_display_ctx.emote_visible = false;
        g_display_ctx.surface_ready = false;

        display_android_notify_owner_change(1, g_display_ctx.lua_width, g_display_ctx.lua_height);

        {
            struct timeval start, now;
            gettimeofday(&start, NULL);
            pthread_mutex_lock(&g_display_ctx.mutex);
            while (!g_display_ctx.surface_ready) {
                struct timespec ts;
                gettimeofday(&now, NULL);
                ts.tv_sec = now.tv_sec + 1;
                ts.tv_nsec = now.tv_usec * 1000;
                pthread_cond_timedwait(&g_display_ctx.surface_ready_cond,
                                       &g_display_ctx.mutex, &ts);
                gettimeofday(&now, NULL);
                if (now.tv_sec - start.tv_sec >= 3) {
                    ESP_LOGW(TAG, "begin_frame fallback: timed out waiting for Java");
                    break;
                }
            }
            pthread_mutex_unlock(&g_display_ctx.mutex);
        }
        ESP_LOGI(TAG, "begin_frame fallback: Lua mode ready");
    }

    if (g_display_ctx.lua_mode && !g_display_ctx.pixels_draw) {
        g_display_ctx.pixels_draw = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);
    }

    uint16_t *dp = draw_pixels();
    if (clear && dp) {
        int total = g_display_ctx.width * g_display_ctx.height;
        for (int i = 0; i < total; i++) {
            dp[i] = color565;
        }
    }

    g_display_ctx.frame_active = true;
    return ESP_OK;
}

/* ---- Forward declarations ---- */
static inline void set_pixel(int x, int y, uint16_t color);

/* ---- Toast overlay (matches desktop draw_toast_overlay) ---- */

static bool g_frame_marked = false;

static void draw_toast_overlay(void)
{
    if (!s_toast_text[0]) return;
    if (g_display_ctx.lua_mode) return;

    int bw = g_display_ctx.width, bh = g_display_ctx.height;
    int toast_x = 40, toast_y = 20, toast_w = 240, toast_h = 40;

    for (int dy = toast_y; dy < toast_y + toast_h && dy < bh; dy++) {
        for (int dx = toast_x; dx < toast_x + toast_w && dx < bw; dx++) {
            set_pixel(dx, dy, 0x0000);
        }
    }

    uint16_t tw, th;
    if (display_hal_measure_text(s_toast_text, 16, &tw, &th) == ESP_OK) {
        int tx = toast_x + (toast_w - (int)tw) / 2;
        int ty = toast_y + (toast_h - (int)th) / 2 - 5;
        display_hal_draw_text(tx, ty, s_toast_text, 16, 0xFFFF, false, 0);
    }
}

esp_err_t display_hal_present(void)
{
    if (!g_display_ctx.pixels) return ESP_FAIL;

    /* Fallback switch to Lua mode — matches display_sdl2.c present() */
    if (!g_display_ctx.lua_mode && !pthread_equal(pthread_self(), s_main_thread)) {
        ESP_LOGI(TAG, "present fallback: switching to Lua %dx%d",
                 g_display_ctx.lua_width, g_display_ctx.lua_height);

        display_arbiter_acquire(DISPLAY_ARBITER_OWNER_LUA);

        if (g_display_ctx.pixels)      { free(g_display_ctx.pixels);      g_display_ctx.pixels = NULL; }
        if (g_display_ctx.pixels_draw) { free(g_display_ctx.pixels_draw); g_display_ctx.pixels_draw = NULL; }

        g_display_ctx.width  = g_display_ctx.lua_width;
        g_display_ctx.height = g_display_ctx.lua_height;
        g_display_ctx.pixels      = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);
        g_display_ctx.pixels_draw = calloc(1, (size_t)g_display_ctx.width * g_display_ctx.height * 2);

        g_display_ctx.lua_mode = true;
        g_display_ctx.emote_visible = false;
        g_display_ctx.surface_ready = false;

        display_android_notify_owner_change(1, g_display_ctx.lua_width, g_display_ctx.lua_height);

        {
            struct timeval start, now;
            gettimeofday(&start, NULL);
            pthread_mutex_lock(&g_display_ctx.mutex);
            while (!g_display_ctx.surface_ready) {
                struct timespec ts;
                gettimeofday(&now, NULL);
                ts.tv_sec = now.tv_sec + 1;
                ts.tv_nsec = now.tv_usec * 1000;
                pthread_cond_timedwait(&g_display_ctx.surface_ready_cond,
                                       &g_display_ctx.mutex, &ts);
                gettimeofday(&now, NULL);
                if (now.tv_sec - start.tv_sec >= 3) {
                    ESP_LOGW(TAG, "present fallback: timed out waiting for Java");
                    break;
                }
            }
            pthread_mutex_unlock(&g_display_ctx.mutex);
        }
        ESP_LOGI(TAG, "present fallback: Lua mode ready");
    }

    /* Only present when the emote thread has marked a new frame ready.
       This eliminates the toast-flicker: mark_frame_ready no longer sends
       a frame to Java; only present() does, and it always draws the toast
       before the copy. */
    if (!g_display_ctx.lua_mode) {
        if (!__atomic_load_n(&g_frame_marked, __ATOMIC_ACQUIRE)) {
            return ESP_OK;
        }
        __atomic_store_n(&g_frame_marked, false, __ATOMIC_RELEASE);
    }

    /* Draw toast overlay on the active draw buffer BEFORE the swap, then
       swap so the copy always reads a complete frame.  This matches the
       desktop behaviour: toast is baked into the presented frame. */
    if (g_display_ctx.pixels_draw) {
        /* Lua double-buffer: draw toast on pixels_draw, then swap */
        uint16_t *saved = g_display_ctx.pixels;
        g_display_ctx.pixels = g_display_ctx.pixels_draw;
        draw_toast_overlay();
        g_display_ctx.pixels = saved;

        uint16_t *tmp = g_display_ctx.pixels;
        g_display_ctx.pixels = g_display_ctx.pixels_draw;
        g_display_ctx.pixels_draw = tmp;
    } else {
        draw_toast_overlay();
    }

    /* Send pixel buffer to Java for display */
    display_android_notify_frame_ready();
    return ESP_OK;
}

esp_err_t display_hal_present_rect(int x, int y, int width, int height)
{
    (void)x; (void)y; (void)width; (void)height;
    /* For simplicity, present full frame. Partial update can be added later. */
    return display_hal_present();
}

esp_err_t display_hal_end_frame(void)
{
    g_display_ctx.frame_active = false;
    return ESP_OK;
}

bool display_hal_is_frame_active(void)
{
    return g_display_ctx.frame_active;
}

void display_hal_wake_main_loop(void)
{
    pthread_mutex_lock(&s_present_mutex);
    g_present_pending = true;
    pthread_cond_broadcast(&s_present_cond);
    pthread_mutex_unlock(&s_present_mutex);
}

void display_hal_main_loop_wait(uint32_t timeout_ms)
{
    pthread_mutex_lock(&s_present_mutex);
    if (!g_present_pending) {
        struct timespec ts;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + (timeout_ms / 1000);
        ts.tv_nsec = (tv.tv_usec + (timeout_ms % 1000) * 1000) * 1000;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&s_present_cond, &s_present_mutex, &ts);
    }
    g_present_pending = false;
    pthread_mutex_unlock(&s_present_mutex);
}

esp_err_t display_hal_get_animation_info(display_hal_animation_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    info->framebuffer_count = g_display_ctx.lua_mode ? 2 : 1;
    info->double_buffered = g_display_ctx.lua_mode;
    info->frame_active = g_display_ctx.frame_active;
    info->flush_in_flight = false;
    return ESP_OK;
}

/* ================================================================
 * Drawing primitives (all to RGB565 pixel buffer)
 * ================================================================ */

static inline void set_pixel(int x, int y, uint16_t color)
{
    uint16_t *dp = draw_pixels();
    if (!dp) return;
    if (x < 0 || x >= g_display_ctx.width) return;
    if (y < 0 || y >= g_display_ctx.height) return;

    if (g_display_ctx.clip_enabled) {
        if (x < g_display_ctx.clip_x || x >= g_display_ctx.clip_x + g_display_ctx.clip_w) return;
        if (y < g_display_ctx.clip_y || y >= g_display_ctx.clip_y + g_display_ctx.clip_h) return;
    }

    dp[y * g_display_ctx.width + x] = color;
}

esp_err_t display_hal_clear(uint16_t color565)
{
    return display_hal_begin_frame(true, color565);
}

esp_err_t display_hal_set_clip_rect(int x, int y, int width, int height)
{
    g_display_ctx.clip_x = x;
    g_display_ctx.clip_y = y;
    g_display_ctx.clip_w = width;
    g_display_ctx.clip_h = height;
    g_display_ctx.clip_enabled = true;
    return ESP_OK;
}

esp_err_t display_hal_clear_clip_rect(void)
{
    g_display_ctx.clip_enabled = false;
    return ESP_OK;
}

esp_err_t display_hal_fill_rect(int x, int y, int width, int height, uint16_t color565)
{
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            set_pixel(x + dx, y + dy, color565);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_pixel(int x, int y, uint16_t color565)
{
    set_pixel(x, y, color565);
    return ESP_OK;
}

esp_err_t display_hal_draw_line(int x0, int y0, int x1, int y1, uint16_t color565)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        set_pixel(x0, y0, color565);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_rect(int x, int y, int w, int h, uint16_t color565)
{
    display_hal_draw_line(x, y, x + w - 1, y, color565);
    display_hal_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color565);
    display_hal_draw_line(x, y, x, y + h - 1, color565);
    display_hal_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color565);
    return ESP_OK;
}

esp_err_t display_hal_set_backlight(bool on)
{
    (void)on;
    return ESP_OK;
}

/* ---- Circle/arc/ellipse/triangle primitives ---- */

esp_err_t display_hal_fill_circle(int cx, int cy, int r, uint16_t color565)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                set_pixel(cx + dx, cy + dy, color565);
            }
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_circle(int cx, int cy, int r, uint16_t color565)
{
    int x = r, y = 0;
    int err = 0;
    while (x >= y) {
        set_pixel(cx + x, cy + y, color565);
        set_pixel(cx + y, cy + x, color565);
        set_pixel(cx - y, cy + x, color565);
        set_pixel(cx - x, cy + y, color565);
        set_pixel(cx - x, cy - y, color565);
        set_pixel(cx - y, cy - x, color565);
        set_pixel(cx + y, cy - x, color565);
        set_pixel(cx + x, cy - y, color565);
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_arc(int cx, int cy, int radius,
                               float start_deg, float end_deg, uint16_t color565)
{
    for (float angle = start_deg; angle <= end_deg; angle += 0.5f) {
        float rad = angle * M_PI / 180.0f;
        int x = cx + (int)(radius * cosf(rad));
        int y = cy + (int)(radius * sinf(rad));
        set_pixel(x, y, color565);
    }
    return ESP_OK;
}

esp_err_t display_hal_fill_arc(int cx, int cy, int inner_radius, int outer_radius,
                               float start_deg, float end_deg, uint16_t color565)
{
    for (int r = inner_radius; r <= outer_radius; r++) {
        for (float angle = start_deg; angle <= end_deg; angle += 0.5f) {
            float rad = angle * M_PI / 180.0f;
            int x = cx + (int)(r * cosf(rad));
            int y = cy + (int)(r * sinf(rad));
            set_pixel(x, y, color565);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_ellipse(int cx, int cy, int rx, int ry, uint16_t color565)
{
    for (float angle = 0; angle < 360; angle += 0.5f) {
        float rad = angle * M_PI / 180.0f;
        int x = cx + (int)(rx * cosf(rad));
        int y = cy + (int)(ry * sinf(rad));
        set_pixel(x, y, color565);
    }
    return ESP_OK;
}

esp_err_t display_hal_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t color565)
{
    for (float r = 0; r <= 1.0f; r += 1.0f / (rx > ry ? rx : ry)) {
        for (float angle = 0; angle < 360; angle += 0.5f) {
            float rad = angle * M_PI / 180.0f;
            int x = cx + (int)(rx * r * cosf(rad));
            int y = cy + (int)(ry * r * sinf(rad));
            set_pixel(x, y, color565);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_round_rect(int x, int y, int w, int h, int r, uint16_t color565)
{
    display_hal_draw_line(x + r, y, x + w - r - 1, y, color565);
    display_hal_draw_line(x + r, y + h - 1, x + w - r - 1, y + h - 1, color565);
    display_hal_draw_line(x, y + r, x, y + h - r - 1, color565);
    display_hal_draw_line(x + w - 1, y + r, x + w - 1, y + h - r - 1, color565);
    display_hal_draw_arc(x + r, y + r, r, 180, 270, color565);
    display_hal_draw_arc(x + w - r - 1, y + r, r, 270, 360, color565);
    display_hal_draw_arc(x + r, y + h - r - 1, r, 90, 180, color565);
    display_hal_draw_arc(x + w - r - 1, y + h - r - 1, r, 0, 90, color565);
    return ESP_OK;
}

esp_err_t display_hal_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color565)
{
    display_hal_fill_rect(x + r, y, w - 2 * r, h, color565);
    display_hal_fill_rect(x, y + r, w, h - 2 * r, color565);
    for (int dy = -r; dy <= r; dy++) {
        int span = (int)sqrtf((float)(r * r - dy * dy));
        if (span < 0) span = 0;
        set_pixel(x + r - span, (y + r + dy), color565); /* TODO: batch fill */
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_triangle(int x1, int y1, int x2, int y2,
                                    int x3, int y3, uint16_t color565)
{
    display_hal_draw_line(x1, y1, x2, y2, color565);
    display_hal_draw_line(x2, y2, x3, y3, color565);
    display_hal_draw_line(x3, y3, x1, y1, color565);
    return ESP_OK;
}

esp_err_t display_hal_fill_triangle(int x1, int y1, int x2, int y2,
                                    int x3, int y3, uint16_t color565)
{
    /* Sort vertices by y */
    if (y1 > y2) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y1 > y3) { int t; t = x1; x1 = x3; x3 = t; t = y1; y1 = y3; y3 = t; }
    if (y2 > y3) { int t; t = x2; x2 = x3; x3 = t; t = y2; y2 = y3; y3 = t; }

    for (int y = y1; y <= y3; y++) {
        float t1 = (y - y1) / (float)(y3 - y1 + 1);
        float t2;
        int xl, xr;
        if (y < y2) {
            t2 = (y - y1) / (float)(y2 - y1 + 1);
            xl = x1 + (int)((x2 - x1) * t2);
            xr = x1 + (int)((x3 - x1) * t1);
        } else {
            t2 = (y - y2) / (float)(y3 - y2 + 1);
            xl = x2 + (int)((x3 - x2) * t2);
            xr = x1 + (int)((x3 - x1) * t1);
        }
        if (xl > xr) { int t = xl; xl = xr; xr = t; }
        for (int x = xl; x <= xr; x++) {
            set_pixel(x, y, color565);
        }
    }
    return ESP_OK;
}

/* ================================================================
 * Text rendering (built-in 8x16 VGA bitmap font)
 * ================================================================ */

/* ---- Text rendering (TrueType via font_android, fallback to built-in) ---- */

esp_err_t display_hal_measure_text(const char *text, uint8_t font_size,
                                   uint16_t *out_width, uint16_t *out_height)
{
    if (!text || !out_width || !out_height) return ESP_ERR_INVALID_ARG;

    /* Use TrueType font system if available */
    if (g_font_android_ok) {
        return font_android_measure_text(text, font_size, out_width, out_height);
    }

    /* Fallback: built-in 8x16 VGA bitmap font */
    int scale = (font_size >= 2) ? 2 : 1;
    int char_w = 8 * scale;
    int char_h = 16 * scale;

    *out_width = (uint16_t)(strlen(text) * char_w);
    *out_height = (uint16_t)char_h;
    return ESP_OK;
}

esp_err_t display_hal_draw_text(int x, int y, const char *text, uint8_t font_size,
                                uint16_t text_color565, bool has_bg, uint16_t bg_color565)
{
    if (!text || !g_display_ctx.pixels) return ESP_ERR_INVALID_ARG;

    /* Use TrueType font system if available */
    if (g_font_android_ok) {
        return font_android_draw_text(x, y, text, font_size,
                                       text_color565, has_bg, bg_color565,
                                       g_display_ctx.width, g_display_ctx.height,
                                       draw_pixels());
    }

    /* Fallback: built-in 8x16 VGA bitmap font */
    int scale = (font_size >= 2) ? 2 : 1;
    int char_w = 8 * scale;
    int char_h = 16 * scale;

    for (size_t ci = 0; ci < strlen(text); ci++) {
        unsigned char c = (unsigned char)text[ci];
        if (c < 32 || c > 127) c = '?';
        const uint8_t *glyph = &builtin_font_8x16[(c - 32) * 16];

        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            set_pixel(x + (int)ci * char_w + col * scale + sx,
                                      y + row * scale + sy,
                                      text_color565);
                        }
                    }
                } else if (has_bg) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            set_pixel(x + (int)ci * char_w + col * scale + sx,
                                      y + row * scale + sy,
                                      bg_color565);
                        }
                    }
                }
            }
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_text_aligned(int x, int y, int w, int h,
                                        const char *text, uint8_t font_size,
                                        uint16_t text_color565, bool has_bg, uint16_t bg_color565,
                                        display_hal_text_align_t align,
                                        display_hal_text_valign_t valign)
{
    if (!text) return ESP_ERR_INVALID_ARG;

    /* Use TrueType font system if available */
    if (g_font_android_ok) {
        return font_android_draw_text_aligned(x, y, w, h, text, font_size,
                                               text_color565, has_bg, bg_color565,
                                               align, valign,
                                               g_display_ctx.width, g_display_ctx.height,
                                               draw_pixels());
    }

    /* Fallback: built-in 8x16 VGA bitmap font */
    uint16_t tw, th;
    display_hal_measure_text(text, font_size, &tw, &th);

    int tx = x, ty = y;

    switch (align) {
    case DISPLAY_HAL_TEXT_ALIGN_LEFT:   tx = x; break;
    case DISPLAY_HAL_TEXT_ALIGN_CENTER: tx = x + (w - tw) / 2; break;
    case DISPLAY_HAL_TEXT_ALIGN_RIGHT:  tx = x + w - tw; break;
    }

    switch (valign) {
    case DISPLAY_HAL_TEXT_VALIGN_TOP:    ty = y; break;
    case DISPLAY_HAL_TEXT_VALIGN_MIDDLE: ty = y + (h - th) / 2; break;
    case DISPLAY_HAL_TEXT_VALIGN_BOTTOM: ty = y + h - th; break;
    }

    return display_hal_draw_text(tx, ty, text, font_size, text_color565, has_bg, bg_color565);
}

/* ================================================================
 * Bitmap
 * ================================================================ */

esp_err_t display_hal_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!pixels) return ESP_ERR_INVALID_ARG;
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            set_pixel(x + dx, y + dy, pixels[dy * w + dx]);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_bitmap_crop(int x, int y,
                                       int src_x, int src_y,
                                       int w, int h,
                                       int src_width, int src_height,
                                       const uint16_t *pixels)
{
    if (!pixels) return ESP_ERR_INVALID_ARG;
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int sx = src_x + dx;
            int sy = src_y + dy;
            if (sx >= 0 && sx < src_width && sy >= 0 && sy < src_height) {
                set_pixel(x + dx, y + dy, pixels[sy * src_width + sx]);
            }
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_bitmap_scaled(int x, int y,
                                         const uint16_t *pixels,
                                         int src_width, int src_height,
                                         int scale_w, int scale_h,
                                         int *out_w, int *out_h)
{
    if (!pixels) return ESP_ERR_INVALID_ARG;

    int dw = src_width * scale_w;
    int dh = src_height * scale_h;
    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;

    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int sx = dx * src_width / dw;
            int sy = dy * src_height / dh;
            set_pixel(x + dx, y + dy, pixels[sy * src_width + sx]);
        }
    }
    return ESP_OK;
}

/* ================================================================
 * JPEG (via stb_image)
 * ================================================================ */

esp_err_t display_hal_jpeg_get_size(const uint8_t *jpeg_data, size_t jpeg_len,
                                    int *out_w, int *out_h)
{
    if (!jpeg_data || !out_w || !out_h) return ESP_ERR_INVALID_ARG;
    int channels;
    if (stbi_info_from_memory(jpeg_data, (int)jpeg_len, out_w, out_h, &channels) == 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_jpeg(int x, int y,
                                const uint8_t *jpeg_data, size_t jpeg_len,
                                int *out_w, int *out_h)
{
    int w, h, channels;
    uint8_t *img = stbi_load_from_memory(jpeg_data, (int)jpeg_len, &w, &h, &channels, 3);
    if (!img) return ESP_FAIL;

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int idx = (dy * w + dx) * 3;
            uint8_t r = img[idx];
            uint8_t g = img[idx + 1];
            uint8_t b = img[idx + 2];
            uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            set_pixel(x + dx, y + dy, c);
        }
    }

    stbi_image_free(img);
    return ESP_OK;
}

esp_err_t display_hal_draw_jpeg_crop(int x, int y,
                                     int src_x, int src_y,
                                     int w, int h,
                                     const uint8_t *jpeg_data, size_t jpeg_len,
                                     int *out_w, int *out_h)
{
    int jw, jh, channels;
    uint8_t *img = stbi_load_from_memory(jpeg_data, (int)jpeg_len, &jw, &jh, &channels, 3);
    if (!img) return ESP_FAIL;

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int sx = src_x + dx;
            int sy = src_y + dy;
            if (sx < 0 || sx >= jw || sy < 0 || sy >= jh) continue;
            int idx = (sy * jw + sx) * 3;
            uint8_t r = img[idx];
            uint8_t g = img[idx + 1];
            uint8_t b = img[idx + 2];
            uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            set_pixel(x + dx, y + dy, c);
        }
    }

    stbi_image_free(img);
    return ESP_OK;
}

esp_err_t display_hal_draw_jpeg_scaled(int x, int y,
                                       const uint8_t *jpeg_data, size_t jpeg_len,
                                       int scale_w, int scale_h,
                                       int *out_w, int *out_h)
{
    int sw, sh, channels;
    uint8_t *img = stbi_load_from_memory(jpeg_data, (int)jpeg_len, &sw, &sh, &channels, 3);
    if (!img) return ESP_FAIL;

    int dw = sw * scale_w;
    int dh = sh * scale_h;
    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;

    for (int dy = 0; dy < dh; dy++) {
        for (int dx = 0; dx < dw; dx++) {
            int sx = dx * sw / dw;
            int sy = dy * sh / dh;
            int idx = (sy * sw + sx) * 3;
            uint8_t r = img[idx];
            uint8_t g = img[idx + 1];
            uint8_t b = img[idx + 2];
            uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            set_pixel(x + dx, y + dy, c);
        }
    }

    stbi_image_free(img);
    return ESP_OK;
}

/* ================================================================
 * Mark frame ready (called from emote engine render thread)
 * ================================================================ */

void display_hal_mark_frame_ready(void)
{
    __atomic_store_n(&g_frame_marked, true, __ATOMIC_RELEASE);
}
