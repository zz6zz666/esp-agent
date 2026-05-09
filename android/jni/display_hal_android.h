/*
 * display_hal_android.h — Android-specific display HAL extensions
 *
 * Provides JNI callback declarations and Android-specific state
 * that supplements the generic display_hal.h interface.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <jni.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Android display context ---- */

typedef struct {
    uint16_t *pixels;       /* RGB565 display buffer (sent to Java) */
    uint16_t *pixels_draw;  /* RGB565 draw buffer (double-buffered in Lua mode) */
    int       width;        /* current buffer width (320 emote, 480 Lua) */
    int       height;       /* current buffer height */
    int       lcd_width;    /* configured LCD size (always 480, from config.json) */
    int       lcd_height;   /* same, used by display_hal_width/height */
    bool      frame_active;
    int       clip_x, clip_y, clip_w, clip_h;
    bool      clip_enabled;

    /* Mode state (mirrors sdl2 sdl_ctx_t) */
    int       lua_width;    /* configured Lua display width */
    int       lua_height;   /* configured Lua display height */
    int       emu_width;    /* emote width (fixed 320) */
    int       emu_height;   /* emote height (fixed 240) */
    bool      lua_mode;     /* true = Lua window active */
    bool      emote_visible;
    bool      emote_was_visible;
    bool      always_hide;
    bool      pending_switch;
    bool      pending_lua_target;

    /* JNI references (cached for callbacks) */
    JavaVM   *jvm;
    jobject   service_obj;          /* FloatingWindowService instance */
    jmethodID mid_on_display_create;
    jmethodID mid_on_display_destroy;
    jmethodID mid_on_owner_changed;
    jmethodID mid_on_frame_ready;
    jmethodID mid_on_emote_text;
    jmethodID mid_on_display_enable;
    jmethodID mid_native_render_text;

    /* Synchronization */
    pthread_mutex_t mutex;
    pthread_cond_t  frame_ready_cond;
    bool            frame_ready_pending;
    bool            surface_ready;   /* true after Java creates the window */
    pthread_cond_t  surface_ready_cond; /* signalled when surface_ready becomes true */
} display_android_ctx_t;

/* ---- Global context ---- */
extern display_android_ctx_t g_display_ctx;
extern bool g_font_android_ok;

/* ---- JNI callback helpers ---- */

/** Initialize JNI cached references from the service object */
esp_err_t display_android_init_jni(JavaVM *jvm, jobject service_obj);

/** Release JNI references */
void display_android_deinit_jni(void);

/** Call back to Java: frame is ready (pixels written) */
void display_android_notify_frame_ready(void);

/** Call back to Java: display created */
void display_android_notify_create(int lua_w, int lua_h);

/** Call back to Java: display destroyed */
void display_android_notify_destroy(void);

/** Call back to Java: display owner changed */
void display_android_notify_owner_change(int owner_mode, int w, int h);

/** Call back to Java: emote text changed */
void display_android_notify_emote_text(const char *text);

/** Call back to Java: toggle display visibility */
void display_android_notify_display_enable(bool enable);

/** Call to Java: render text using Android native font engine.
 *  Returns malloc'd RGBA32 byte array + prepended [w:int][h:int] header,
 *  or NULL on failure.  Caller must free the returned buffer. */
uint8_t *display_android_render_text(const char *text, int font_size, int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif
