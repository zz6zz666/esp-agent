/*
 * display_sdl2.c — SDL2 implementation of display_hal.h for desktop simulator
 *
 * All drawing uses a back-buffer SDL_Surface in RGB565 (matching ESP LCD format).
 * On present(), the surface is converted to an SDL_Texture and rendered.
 */
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "display_hal.h"
#include "display_hal_input.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "display_sdl2";

/* ---- Font stack (multi-font fallback for emoji / symbols / CJK) ---- */
#define MAX_FONT_STACK 4

static const char *s_font_paths[MAX_FONT_STACK] = {0};
static const char *s_font_labels[MAX_FONT_STACK] = {0};
static int          s_font_path_count = 0;
static TTF_Font    *s_font_stack[MAX_FONT_STACK] = {0};
static int          s_font_stack_count = 0;
static int          s_font_stack_ptsize = 0;

/* ---- Internal state ---- */
typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    SDL_Surface  *surface;    /* RGB565 back-buffer (size = current mode) */
    int width;                /* configured LCD size (Agent/Lua visible) */
    int height;
    int emu_width;            /* emote size (fixed 320) */
    int emu_height;           /* emote size (fixed 240) */
    int surf_w, surf_h;       /* actual surface/texture dimensions */
    int expected_w, expected_h; /* locked window size (reject external resize) */
    bool frame_active;
    bool lua_mode;            /* true = Lua window active, false = emote */
    bool pending_switch;      /* true = destroy+recreate window next present */
    bool pending_lua_target;  /* target mode for pending_switch */
    int clip_x, clip_y, clip_w, clip_h;
    bool clip_enabled;
    /* Lifecycle ops delegated from worker thread to main thread */
    bool lifecycle_pending;
    int  lifecycle_op;        /* 0=none, 1=create, 2=destroy */
    int  lifecycle_w, lifecycle_h;
    bool lifecycle_done;
    esp_err_t lifecycle_result;
    pthread_mutex_t lifecycle_mutex;
    pthread_cond_t lifecycle_cond;
} sdl_ctx_t;

/* ---- Input state (SDL → touch bridge + HAL input API) ---- */

typedef struct {
    int16_t x, y;            /* logical display coords (scaled from window) */
    bool left, middle, right;
    int wheel;               /* accumulated scroll ticks */
    bool moved;
} mouse_state_t;

typedef struct {
    mouse_state_t mouse;
    uint8_t keys[SDL_NUM_SCANCODES];  /* 0=up, 1=down */
    uint16_t modifiers;               /* active SDL_Keymod bits */
    input_event_t queue[INPUT_EVENT_QUEUE_SIZE];
    int queue_head, queue_count;
    pthread_mutex_t mutex;
    int window_w, window_h;           /* current window pixel size */
    float scale_x, scale_y;           /* window / logical */
} input_state_t;

static sdl_ctx_t s_ctx = {0};
static input_state_t s_input = {0};
static pthread_t s_main_thread;
static pthread_mutex_t s_present_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_present_cond = PTHREAD_COND_INITIALIZER;
static volatile bool g_present_pending = false;
static bool s_ttf_ok = false;

/* ---- Glyph cache (avoids re-rasterizing emoji / text every frame) ---- */
#define GLYPH_CACHE_MAX 512

typedef struct {
    Uint32      codepoint;
    int         ptsize;
    int         font_idx;       /* index into s_font_stack */
    uint16_t    fg_color565;
    SDL_Surface *surf;          /* owned, pre-scaled RGBA32 */
    int         last_used;
} glyph_cache_entry_t;

static glyph_cache_entry_t s_glyph_cache[GLYPH_CACHE_MAX];
static int                 s_glyph_cache_count = 0;
static int                 s_glyph_cache_tick  = 0;
static pthread_mutex_t     s_glyph_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/* forward declarations */
esp_err_t display_hal_destroy(void);
static volatile bool g_display_quit_requested = false;
static void font_stack_close(void);
static void glyph_cache_clear(void);
static void glyph_cache_load_all(void);
static int  font_stack_load(int ptsize);
static TTF_Font *font_for_codepoint(Uint32 cp);
static Uint32 utf8_decode(const char **p);
static int  utf8_encode(Uint32 cp, char *buf);
static SDL_Surface *glyph_cache_lookup(Uint32 cp, int ptsize,
                                        int font_idx, uint16_t color565);
static void glyph_cache_insert(Uint32 cp, int ptsize, int font_idx,
                                uint16_t color565, SDL_Surface *surf);

void display_hal_mark_frame_ready(void)
{
    /* Legacy: the main loop now renders every tick regardless. */
}

bool display_hal_is_active(void)
{
    return s_ctx.window != NULL;
}

bool display_hal_quit_requested(void)
{
    bool v = g_display_quit_requested;
    g_display_quit_requested = false;
    return v;
}

/* ---- RGB565 helpers ---- */
static inline uint16_t rgb_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void rgb565_to_rgb(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((c >> 8) & 0xF8);
    *g = (uint8_t)((c >> 3) & 0xFC);
    *b = (uint8_t)((c << 3) & 0xF8);
}

static void put_pixel(int x, int y, uint16_t color)
{
    if (!s_ctx.surface) return;
    if (x < 0 || x >= s_ctx.surf_w || y < 0 || y >= s_ctx.surf_h) return;
    if (s_ctx.clip_enabled) {
        if (x < s_ctx.clip_x || x >= s_ctx.clip_x + s_ctx.clip_w ||
            y < s_ctx.clip_y || y >= s_ctx.clip_y + s_ctx.clip_h) return;
    }
    uint16_t *pixels = (uint16_t *)s_ctx.surface->pixels;
    pixels[y * s_ctx.surf_w + x] = color;
}

/* ---- Lifecycle ---- */

/* Recreate surface + texture at the given size.  Caller must hold
 * s_present_mutex and be on the main thread. */
static esp_err_t recreate_surface(int w, int h)
{
    if (s_ctx.surface) SDL_FreeSurface(s_ctx.surface);
    if (s_ctx.texture) SDL_DestroyTexture(s_ctx.texture);

    s_ctx.surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 16,
                                                    SDL_PIXELFORMAT_RGB565);
    if (!s_ctx.surface) {
        ESP_LOGE(TAG, "recreate surface (%dx%d) failed: %s", w, h, SDL_GetError());
        return ESP_ERR_NO_MEM;
    }

    s_ctx.texture = SDL_CreateTexture(s_ctx.renderer,
                                       SDL_PIXELFORMAT_RGB565,
                                       SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!s_ctx.texture) {
        ESP_LOGE(TAG, "recreate texture (%dx%d) failed: %s", w, h, SDL_GetError());
        SDL_FreeSurface(s_ctx.surface);
        s_ctx.surface = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_ctx.surf_w = w;
    s_ctx.surf_h = h;
    return ESP_OK;
}

esp_err_t display_hal_create(esp_lcd_panel_handle_t panel_handle,
                             esp_lcd_panel_io_handle_t io_handle,
                             display_hal_panel_if_t panel_if,
                             int lcd_width, int lcd_height)
{
    (void)panel_handle; (void)io_handle; (void)panel_if;

    int lcd_w = lcd_width  > 0 ? lcd_width  : 480;
    int lcd_h = lcd_height > 0 ? lcd_height : 480;

    /* If already on the main thread, do SDL ops directly (startup path) */
    if (pthread_equal(pthread_self(), s_main_thread) || s_main_thread == 0) {
        if (s_ctx.window) {
            if (s_ctx.width == lcd_w && s_ctx.height == lcd_h) {
                return ESP_OK;
            }
            display_hal_destroy();
        }

        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            ESP_LOGE(TAG, "SDL_Init failed: %s", SDL_GetError());
            return ESP_FAIL;
        }

        s_ctx.width  = lcd_w;
        s_ctx.height = lcd_h;
        s_ctx.emu_width  = 320;
        s_ctx.emu_height = 240;

        s_ctx.expected_w = s_ctx.emu_width;
        s_ctx.expected_h = s_ctx.emu_height;
        s_ctx.lua_mode = false;

        s_ctx.window = SDL_CreateWindow("esp-claw Desktop Simulator",
                                         SDL_WINDOWPOS_UNDEFINED,
                                         SDL_WINDOWPOS_UNDEFINED,
                                         s_ctx.emu_width, s_ctx.emu_height,
                                         SDL_WINDOW_SHOWN);
        if (!s_ctx.window) {
            ESP_LOGE(TAG, "SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return ESP_FAIL;
        }

        s_ctx.renderer = SDL_CreateRenderer(s_ctx.window, -1,
                                             SDL_RENDERER_ACCELERATED |
                                             SDL_RENDERER_PRESENTVSYNC);
        if (!s_ctx.renderer) {
            ESP_LOGE(TAG, "SDL_CreateRenderer failed: %s", SDL_GetError());
            SDL_DestroyWindow(s_ctx.window);
            SDL_Quit();
            return ESP_FAIL;
        }
        SDL_SetRenderDrawColor(s_ctx.renderer, 0, 0, 0, 255);

        if (recreate_surface(s_ctx.emu_width, s_ctx.emu_height) != ESP_OK) {
            SDL_DestroyRenderer(s_ctx.renderer);
            SDL_DestroyWindow(s_ctx.window);
            SDL_Quit();
            return ESP_FAIL;
        }

        s_main_thread = pthread_self();
        goto init_fonts_and_input;
    }

    /* ---- Worker thread path: delegate to main thread ----

       Use the configured LCD size (s_ctx.width/height) rather than
       the passed hardware params.  The Lua module passes the board's
       hardware LCD size (320x240 from board_manager), but the Lua
       display window should use the configurable virtual LCD size
       (default 480x480, matching config.json). */

    /* Store params and request main-thread creation */
    pthread_mutex_lock(&s_ctx.lifecycle_mutex);
    s_ctx.lifecycle_w = s_ctx.width;
    s_ctx.lifecycle_h = s_ctx.height;
    s_ctx.lifecycle_op = 1; /* create */
    s_ctx.lifecycle_pending = true;
    s_ctx.lifecycle_done = false;

    /* Wake main loop */
    pthread_mutex_lock(&s_present_mutex);
    g_present_pending = true;
    pthread_cond_broadcast(&s_present_cond);
    pthread_mutex_unlock(&s_present_mutex);

    /* Wait for main thread to complete */
    while (!s_ctx.lifecycle_done) {
        pthread_cond_wait(&s_ctx.lifecycle_cond, &s_ctx.lifecycle_mutex);
    }
    s_ctx.lifecycle_pending = false;
    esp_err_t result = s_ctx.lifecycle_result;
    pthread_mutex_unlock(&s_ctx.lifecycle_mutex);
    return result;

init_fonts_and_input:
    /* TTF font stack for text rendering (CJK + emoji + symbols fallback) */
    if (TTF_Init() < 0) {
        ESP_LOGW(TAG, "TTF_Init failed: %s", TTF_GetError());
        s_ttf_ok = false;
    } else {
        struct {
            const char *path;
            const char *label;
        } candidates[] = {
            { "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",       "WQY ZenHei" },
            { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",    "DejaVu Sans" },
            { "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",  "Noto Color Emoji" },
        };
        int n = sizeof(candidates) / sizeof(candidates[0]);

        for (int i = 0; i < n && s_font_path_count < MAX_FONT_STACK; i++) {
            TTF_Font *test = TTF_OpenFont(candidates[i].path, 16);
            if (test) {
                TTF_CloseFont(test);
                s_font_paths[s_font_path_count]     = candidates[i].path;
                s_font_labels[s_font_path_count]    = candidates[i].label;
                s_font_path_count++;
            } else {
                ESP_LOGW(TAG, "Font not found, skipping: %s (%s)",
                         candidates[i].label, candidates[i].path);
            }
        }

        if (s_font_path_count > 0) {
            s_ttf_ok = true;
            font_stack_load(16);
            glyph_cache_load_all();
            ESP_LOGI(TAG, "TTF font stack loaded (%d fonts)", s_font_path_count);
        } else {
            ESP_LOGW(TAG, "No TTF fonts found — text will not render");
            s_ttf_ok = false;
        }
    }

    /* Init input state (1:1 scale — no window scaling ever) */
    pthread_mutex_init(&s_input.mutex, NULL);
    pthread_mutex_init(&s_ctx.lifecycle_mutex, NULL);
    pthread_cond_init(&s_ctx.lifecycle_cond, NULL);
    s_input.window_w = s_ctx.emu_width;
    s_input.window_h = s_ctx.emu_height;
    s_input.scale_x = 1.0f;
    s_input.scale_y = 1.0f;
    esp_lcd_touch_init_sdl();

    ESP_LOGI(TAG, "Display created: LCD=%dx%d emu=%dx%d (main_thread=%lu)",
             s_ctx.width, s_ctx.height, s_ctx.emu_width, s_ctx.emu_height,
             (unsigned long)s_main_thread);
    return ESP_OK;
}

esp_err_t display_hal_destroy(void)
{
    /* Main thread path: destroy SDL resources directly (shutdown) */
    if (pthread_equal(pthread_self(), s_main_thread)) {
        if (s_ctx.texture)  { SDL_DestroyTexture(s_ctx.texture);   s_ctx.texture = NULL; }
        if (s_ctx.surface)  { SDL_FreeSurface(s_ctx.surface);      s_ctx.surface = NULL; }
        if (s_ctx.renderer) { SDL_DestroyRenderer(s_ctx.renderer); s_ctx.renderer = NULL; }
        if (s_ctx.window)   { SDL_DestroyWindow(s_ctx.window);     s_ctx.window = NULL; }
        s_ctx.surf_w = 0;
        s_ctx.surf_h = 0;
        s_ctx.frame_active = false;
        s_ctx.lua_mode = false;
        s_ctx.pending_switch = false;
        return ESP_OK;
    }

    /* Worker thread path (Lua deinit): delegate to main thread.
       If Lua window is active, the main thread will destroy it and
       re-create the emote window so the emote engine can resume. */
    pthread_mutex_lock(&s_ctx.lifecycle_mutex);
    s_ctx.lifecycle_op = 2; /* destroy / switch back to emote */
    s_ctx.lifecycle_pending = true;
    s_ctx.lifecycle_done = false;

    /* Wake main loop */
    pthread_mutex_lock(&s_present_mutex);
    g_present_pending = true;
    pthread_cond_broadcast(&s_present_cond);
    pthread_mutex_unlock(&s_present_mutex);

    /* Wait for main thread to complete */
    while (!s_ctx.lifecycle_done) {
        pthread_cond_wait(&s_ctx.lifecycle_cond, &s_ctx.lifecycle_mutex);
    }
    s_ctx.lifecycle_pending = false;
    esp_err_t result = s_ctx.lifecycle_result;
    pthread_mutex_unlock(&s_ctx.lifecycle_mutex);
    return result;
}

/* ---- Geometry ---- */

int display_hal_width(void)  { return s_ctx.width; }
int display_hal_height(void) { return s_ctx.height; }

/* ---- Frame control ---- */

esp_err_t display_hal_begin_frame(bool clear, uint16_t color565)
{
    /* Trigger switch to Lua-sized window (destroy+create on main thread) */
    if (!s_ctx.lua_mode) {
        s_ctx.pending_switch = true;
        s_ctx.pending_lua_target = true;
    }
    /* Do NOT clear old surface here — it's the wrong size until the main
       thread processes the switch.  The new surface is cleared after creation. */
    (void)clear;
    (void)color565;
    s_ctx.frame_active = true;
    return ESP_OK;
}

/* Process all pending SDL events.  Returns true if SDL_QUIT was seen. */
static bool process_sdl_events(void)
{
    SDL_Event ev;
    bool quit_seen = false;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            ESP_LOGI(TAG, "Window closed by user");
            g_display_quit_requested = true;
            quit_seen = true;
            break;

        case SDL_MOUSEMOTION:
            pthread_mutex_lock(&s_input.mutex);
            s_input.mouse.x = (int16_t)(ev.motion.x / s_input.scale_x);
            s_input.mouse.y = (int16_t)(ev.motion.y / s_input.scale_y);
            s_input.mouse.moved = true;
            pthread_mutex_unlock(&s_input.mutex);
            /* Feed to touch bridge when button is held */
            if (s_input.mouse.left || s_input.mouse.right) {
                esp_lcd_touch_feed_sdl(s_input.mouse.x, s_input.mouse.y, true);
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            bool pressed = (ev.type == SDL_MOUSEBUTTONDOWN);
            pthread_mutex_lock(&s_input.mutex);
            int16_t mx = (int16_t)(ev.button.x / s_input.scale_x);
            int16_t my = (int16_t)(ev.button.y / s_input.scale_y);
            s_input.mouse.x = mx;
            s_input.mouse.y = my;
            switch (ev.button.button) {
            case SDL_BUTTON_LEFT:   s_input.mouse.left = pressed; break;
            case SDL_BUTTON_MIDDLE: s_input.mouse.middle = pressed; break;
            case SDL_BUTTON_RIGHT:  s_input.mouse.right = pressed; break;
            }
            /* Enqueue event */
            if (s_input.queue_count < INPUT_EVENT_QUEUE_SIZE) {
                int idx = (s_input.queue_head + s_input.queue_count)
                          % INPUT_EVENT_QUEUE_SIZE;
                s_input.queue[idx].type = pressed
                    ? INPUT_EVENT_MOUSE_DOWN : INPUT_EVENT_MOUSE_UP;
                s_input.queue[idx].x = mx;
                s_input.queue[idx].y = my;
                s_input.queue[idx].button = ev.button.button;
                s_input.queue[idx].mod = s_input.modifiers;
                s_input.queue_count++;
            }
            pthread_mutex_unlock(&s_input.mutex);
            /* Feed touch bridge */
            esp_lcd_touch_feed_sdl(mx, my, pressed);
            break;
        }

        case SDL_MOUSEWHEEL:
            pthread_mutex_lock(&s_input.mutex);
            s_input.mouse.wheel += ev.wheel.y;
            if (s_input.queue_count < INPUT_EVENT_QUEUE_SIZE) {
                int idx = (s_input.queue_head + s_input.queue_count)
                          % INPUT_EVENT_QUEUE_SIZE;
                s_input.queue[idx].type = INPUT_EVENT_MOUSE_WHEEL;
                s_input.queue[idx].x = s_input.mouse.x;
                s_input.queue[idx].y = s_input.mouse.y;
                s_input.queue[idx].button = ev.wheel.y;
                s_input.queue[idx].mod = s_input.modifiers;
                s_input.queue_count++;
            }
            pthread_mutex_unlock(&s_input.mutex);
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool pressed = (ev.type == SDL_KEYDOWN);
            SDL_Scancode sc = ev.key.keysym.scancode;
            pthread_mutex_lock(&s_input.mutex);
            if (sc < SDL_NUM_SCANCODES) {
                s_input.keys[sc] = pressed ? 1 : 0;
            }
            s_input.modifiers = ev.key.keysym.mod;
            if (s_input.queue_count < INPUT_EVENT_QUEUE_SIZE) {
                int idx = (s_input.queue_head + s_input.queue_count)
                          % INPUT_EVENT_QUEUE_SIZE;
                s_input.queue[idx].type = pressed
                    ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP;
                s_input.queue[idx].key = sc;
                s_input.queue[idx].mod = ev.key.keysym.mod;
                s_input.queue[idx].x = s_input.mouse.x;
                s_input.queue[idx].y = s_input.mouse.y;
                s_input.queue_count++;
            }
            pthread_mutex_unlock(&s_input.mutex);
            break;
        }

        case SDL_TEXTINPUT:
            pthread_mutex_lock(&s_input.mutex);
            if (s_input.queue_count < INPUT_EVENT_QUEUE_SIZE) {
                int idx = (s_input.queue_head + s_input.queue_count)
                          % INPUT_EVENT_QUEUE_SIZE;
                s_input.queue[idx].type = INPUT_EVENT_TEXT;
                strncpy(s_input.queue[idx].text, ev.text.text,
                        INPUT_TEXT_MAX - 1);
                s_input.queue[idx].text[INPUT_TEXT_MAX - 1] = '\0';
                s_input.queue[idx].x = s_input.mouse.x;
                s_input.queue[idx].y = s_input.mouse.y;
                s_input.queue[idx].mod = s_input.modifiers;
                s_input.queue_count++;
            }
            pthread_mutex_unlock(&s_input.mutex);
            break;

        case SDL_WINDOWEVENT:
            /* Reject any external window resize/maximize — force back */
            if (s_ctx.window && (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                ev.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
                int cur_w, cur_h;
                SDL_GetWindowSize(s_ctx.window, &cur_w, &cur_h);
                if (cur_w != s_ctx.expected_w || cur_h != s_ctx.expected_h) {
                    SDL_SetWindowSize(s_ctx.window,
                                      s_ctx.expected_w, s_ctx.expected_h);
                }
            }
            break;
        }
    }
    return quit_seen;
}

esp_err_t display_hal_present(void)
{
    if (pthread_equal(pthread_self(), s_main_thread)) {
        /* ---- Handle pending lifecycle ops (delegated from worker thread) ---- */
        if (s_ctx.lifecycle_pending) {
            pthread_mutex_lock(&s_ctx.lifecycle_mutex);
            int op = s_ctx.lifecycle_op;
            int lw = s_ctx.lifecycle_w;
            int lh = s_ctx.lifecycle_h;
            pthread_mutex_unlock(&s_ctx.lifecycle_mutex);

            esp_err_t lc_result = ESP_OK;

            if (op == 1) {
                /* create: destroy current window, create new one at Lua LCD size */
                if (s_ctx.texture)  { SDL_DestroyTexture(s_ctx.texture);   s_ctx.texture = NULL; }
                if (s_ctx.surface)  { SDL_FreeSurface(s_ctx.surface);      s_ctx.surface = NULL; }
                if (s_ctx.renderer) { SDL_DestroyRenderer(s_ctx.renderer); s_ctx.renderer = NULL; }
                if (s_ctx.window)   { SDL_DestroyWindow(s_ctx.window);     s_ctx.window = NULL; }
                s_ctx.frame_active = false;
                s_ctx.pending_switch = false;

                s_ctx.width  = lw;
                s_ctx.height = lh;

                s_ctx.window = SDL_CreateWindow("esp-claw Lua Display",
                                                 SDL_WINDOWPOS_UNDEFINED,
                                                 SDL_WINDOWPOS_UNDEFINED,
                                                 lw, lh, SDL_WINDOW_SHOWN);
                if (!s_ctx.window) {
                    ESP_LOGE(TAG, "Lifecycle create: SDL_CreateWindow(%dx%d) failed: %s",
                             lw, lh, SDL_GetError());
                    lc_result = ESP_FAIL;
                    goto lifecycle_done;
                }

                s_ctx.renderer = SDL_CreateRenderer(s_ctx.window, -1,
                                                     SDL_RENDERER_ACCELERATED |
                                                     SDL_RENDERER_PRESENTVSYNC);
                if (!s_ctx.renderer) {
                    ESP_LOGE(TAG, "Lifecycle create: SDL_CreateRenderer failed: %s",
                             SDL_GetError());
                    SDL_DestroyWindow(s_ctx.window);
                    s_ctx.window = NULL;
                    lc_result = ESP_FAIL;
                    goto lifecycle_done;
                }
                SDL_SetRenderDrawColor(s_ctx.renderer, 0, 0, 0, 255);

                if (recreate_surface(lw, lh) != ESP_OK) {
                    SDL_DestroyRenderer(s_ctx.renderer);
                    SDL_DestroyWindow(s_ctx.window);
                    s_ctx.renderer = NULL;
                    s_ctx.window = NULL;
                    lc_result = ESP_FAIL;
                    goto lifecycle_done;
                }
                SDL_FillRect(s_ctx.surface, NULL, 0x0000);

                s_ctx.lua_mode = true;
                s_ctx.expected_w = lw;
                s_ctx.expected_h = lh;

                pthread_mutex_lock(&s_input.mutex);
                s_input.window_w = lw;
                s_input.window_h = lh;
                s_input.scale_x = 1.0f;
                s_input.scale_y = 1.0f;
                pthread_mutex_unlock(&s_input.mutex);

                ESP_LOGI(TAG, "Window created for Lua: %dx%d", lw, lh);

            } else if (op == 2) {
                /* destroy (from Lua deinit): switch back to emote window */
                bool was_lua = s_ctx.lua_mode;

                if (s_ctx.window) {
                    if (s_ctx.texture)  { SDL_DestroyTexture(s_ctx.texture);   s_ctx.texture = NULL; }
                    if (s_ctx.surface)  { SDL_FreeSurface(s_ctx.surface);      s_ctx.surface = NULL; }
                    if (s_ctx.renderer) { SDL_DestroyRenderer(s_ctx.renderer); s_ctx.renderer = NULL; }
                    if (s_ctx.window)   { SDL_DestroyWindow(s_ctx.window);     s_ctx.window = NULL; }
                }
                s_ctx.frame_active = false;
                s_ctx.lua_mode = false;
                s_ctx.pending_switch = false;

                /* Always re-create the emote window so emote engine can resume */
                s_ctx.window = SDL_CreateWindow("esp-claw Desktop Simulator",
                                                 SDL_WINDOWPOS_UNDEFINED,
                                                 SDL_WINDOWPOS_UNDEFINED,
                                                 s_ctx.emu_width, s_ctx.emu_height,
                                                 SDL_WINDOW_SHOWN);
                if (!s_ctx.window) {
                    ESP_LOGE(TAG, "Lifecycle destroy: SDL_CreateWindow failed: %s",
                             SDL_GetError());
                    lc_result = ESP_FAIL;
                    goto lifecycle_done;
                }

                s_ctx.renderer = SDL_CreateRenderer(s_ctx.window, -1,
                                                     SDL_RENDERER_ACCELERATED |
                                                     SDL_RENDERER_PRESENTVSYNC);
                if (!s_ctx.renderer) {
                    ESP_LOGE(TAG, "Lifecycle destroy: SDL_CreateRenderer failed: %s",
                             SDL_GetError());
                    SDL_DestroyWindow(s_ctx.window);
                    s_ctx.window = NULL;
                    lc_result = ESP_FAIL;
                    goto lifecycle_done;
                }
                SDL_SetRenderDrawColor(s_ctx.renderer, 0, 0, 0, 255);

                if (recreate_surface(s_ctx.emu_width, s_ctx.emu_height) != ESP_OK) {
                    SDL_DestroyRenderer(s_ctx.renderer);
                    SDL_DestroyWindow(s_ctx.window);
                    s_ctx.renderer = NULL;
                    s_ctx.window = NULL;
                    lc_result = ESP_FAIL;
                    goto lifecycle_done;
                }
                SDL_FillRect(s_ctx.surface, NULL, 0x0000);

                s_ctx.expected_w = s_ctx.emu_width;
                s_ctx.expected_h = s_ctx.emu_height;

                pthread_mutex_lock(&s_input.mutex);
                s_input.window_w = s_ctx.emu_width;
                s_input.window_h = s_ctx.emu_height;
                s_input.scale_x = 1.0f;
                s_input.scale_y = 1.0f;
                pthread_mutex_unlock(&s_input.mutex);

                ESP_LOGI(TAG, "Window switched back to emote %dx%d (was_lua=%d)",
                         s_ctx.emu_width, s_ctx.emu_height, was_lua);
            }

        lifecycle_done:
            pthread_mutex_lock(&s_ctx.lifecycle_mutex);
            s_ctx.lifecycle_done = true;
            s_ctx.lifecycle_result = lc_result;
            pthread_cond_broadcast(&s_ctx.lifecycle_cond);
            pthread_mutex_unlock(&s_ctx.lifecycle_mutex);

            /* Skip rendering this tick — new window has fresh black surface */
            process_sdl_events();
            return lc_result;
        }

        /* ---- Handle pending mode switch (begin_frame → Lua window) ---- */
        if (s_ctx.pending_switch) {
            s_ctx.pending_switch = false;
            bool target_lua = s_ctx.pending_lua_target;
            int new_w = target_lua ? s_ctx.width  : s_ctx.emu_width;
            int new_h = target_lua ? s_ctx.height : s_ctx.emu_height;

            /* Destroy current window and all associated resources */
            if (s_ctx.texture)  { SDL_DestroyTexture(s_ctx.texture);   s_ctx.texture = NULL; }
            if (s_ctx.surface)  { SDL_FreeSurface(s_ctx.surface);      s_ctx.surface = NULL; }
            if (s_ctx.renderer) { SDL_DestroyRenderer(s_ctx.renderer); s_ctx.renderer = NULL; }
            if (s_ctx.window)   { SDL_DestroyWindow(s_ctx.window);     s_ctx.window = NULL; }
            s_ctx.frame_active = false;

            /* Create new window at target size */
            const char *title = target_lua
                ? "esp-claw Lua Display"
                : "esp-claw Desktop Simulator";
            s_ctx.window = SDL_CreateWindow(title,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             new_w, new_h,
                                             SDL_WINDOW_SHOWN);
            if (!s_ctx.window) {
                ESP_LOGE(TAG, "Mode switch: SDL_CreateWindow(%dx%d) failed: %s",
                         new_w, new_h, SDL_GetError());
                SDL_Quit();
                return ESP_FAIL;
            }

            s_ctx.renderer = SDL_CreateRenderer(s_ctx.window, -1,
                                                 SDL_RENDERER_ACCELERATED |
                                                 SDL_RENDERER_PRESENTVSYNC);
            if (!s_ctx.renderer) {
                ESP_LOGE(TAG, "Mode switch: SDL_CreateRenderer failed: %s",
                         SDL_GetError());
                SDL_DestroyWindow(s_ctx.window);
                s_ctx.window = NULL;
                SDL_Quit();
                return ESP_FAIL;
            }
            SDL_SetRenderDrawColor(s_ctx.renderer, 0, 0, 0, 255);

            if (recreate_surface(new_w, new_h) != ESP_OK) {
                SDL_DestroyRenderer(s_ctx.renderer);
                SDL_DestroyWindow(s_ctx.window);
                s_ctx.renderer = NULL;
                s_ctx.window = NULL;
                SDL_Quit();
                return ESP_FAIL;
            }

            /* Clear the new surface to black */
            SDL_FillRect(s_ctx.surface, NULL, 0x0000);

            s_ctx.lua_mode = target_lua;
            s_ctx.expected_w = new_w;
            s_ctx.expected_h = new_h;

            /* Update input state for new window (1:1 scale) */
            pthread_mutex_lock(&s_input.mutex);
            s_input.window_w = new_w;
            s_input.window_h = new_h;
            s_input.scale_x = 1.0f;
            s_input.scale_y = 1.0f;
            pthread_mutex_unlock(&s_input.mutex);

            ESP_LOGI(TAG, "Window switched to %dx%d (mode=%s)",
                     new_w, new_h, target_lua ? "lua" : "emote");
        }

        /* ---- Render current frame (always render when surface exists) ---- */
        if (s_ctx.texture && s_ctx.surface) {
            SDL_UpdateTexture(s_ctx.texture, NULL,
                              s_ctx.surface->pixels, s_ctx.surf_w * 2);
            SDL_RenderClear(s_ctx.renderer);
            SDL_RenderCopy(s_ctx.renderer, s_ctx.texture, NULL, NULL);
            SDL_RenderPresent(s_ctx.renderer);
        }

        /* Signal any waiting worker thread that the frame was presented */
        pthread_mutex_lock(&s_present_mutex);
        if (g_present_pending) {
            g_present_pending = false;
            pthread_cond_broadcast(&s_present_cond);
        }
        pthread_mutex_unlock(&s_present_mutex);

        process_sdl_events();
    } else {
        /* Worker thread (Lua): signal that a frame is ready and block
           until the main loop renders it. */
        pthread_mutex_lock(&s_present_mutex);
        g_present_pending = true;
        pthread_cond_broadcast(&s_present_cond);
        while (g_present_pending) {
            pthread_cond_wait(&s_present_cond, &s_present_mutex);
        }
        pthread_mutex_unlock(&s_present_mutex);
    }
    return ESP_OK;
}

esp_err_t display_hal_present_rect(int x, int y, int width, int height)
{
    if (!s_ctx.texture || !s_ctx.surface) return ESP_ERR_INVALID_STATE;

    /* SDL2 GPU rendering must happen on the main thread only. */
    if (!pthread_equal(pthread_self(), s_main_thread)) {
        return ESP_OK;
    }

    SDL_Rect r = { x, y, width, height };
    SDL_Surface *sub = SDL_CreateRGBSurfaceWithFormatFrom(
        (uint8_t *)s_ctx.surface->pixels + (y * s_ctx.surf_w + x) * 2,
        width, height, 16, s_ctx.surf_w * 2, SDL_PIXELFORMAT_RGB565);
    if (!sub) return ESP_ERR_NO_MEM;
    SDL_UpdateTexture(s_ctx.texture, &r, sub->pixels, sub->pitch);
    SDL_RenderClear(s_ctx.renderer);
    SDL_RenderCopy(s_ctx.renderer, s_ctx.texture, NULL, NULL);
    SDL_RenderPresent(s_ctx.renderer);
    SDL_FreeSurface(sub);

    process_sdl_events();
    return ESP_OK;
}

esp_err_t display_hal_end_frame(void)
{
    s_ctx.frame_active = false;
    return ESP_OK;
}

bool display_hal_is_frame_active(void)
{
    return s_ctx.frame_active;
}

esp_err_t display_hal_get_animation_info(display_hal_animation_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    info->framebuffer_count = 1;
    info->double_buffered = false;
    info->frame_active = s_ctx.frame_active;
    info->flush_in_flight = false;
    return ESP_OK;
}

/* ---- Drawing primitives ---- */

esp_err_t display_hal_clear(uint16_t color565)
{
    SDL_FillRect(s_ctx.surface, NULL, color565);
    return ESP_OK;
}

esp_err_t display_hal_set_clip_rect(int x, int y, int width, int height)
{
    s_ctx.clip_enabled = true;
    s_ctx.clip_x = x;
    s_ctx.clip_y = y;
    s_ctx.clip_w = width;
    s_ctx.clip_h = height;
    return ESP_OK;
}

esp_err_t display_hal_clear_clip_rect(void)
{
    s_ctx.clip_enabled = false;
    return ESP_OK;
}

esp_err_t display_hal_fill_rect(int x, int y, int w, int h, uint16_t color565)
{
    SDL_Rect r = { x, y, w, h };
    SDL_FillRect(s_ctx.surface, &r, color565);
    return ESP_OK;
}

esp_err_t display_hal_draw_line(int x0, int y0, int x1, int y1, uint16_t color565)
{
    /* Bresenham */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        put_pixel(x0, y0, color565);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_rect(int x, int y, int w, int h, uint16_t color565)
{
    display_hal_draw_line(x, y, x + w - 1, y, color565);
    display_hal_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color565);
    display_hal_draw_line(x + w - 1, y + h - 1, x, y + h - 1, color565);
    display_hal_draw_line(x, y + h - 1, x, y, color565);
    return ESP_OK;
}

esp_err_t display_hal_draw_pixel(int x, int y, uint16_t color565)
{
    put_pixel(x, y, color565);
    return ESP_OK;
}

esp_err_t display_hal_set_backlight(bool on)
{
    (void)on;
    return ESP_OK;
}

esp_err_t display_hal_fill_circle(int cx, int cy, int r, uint16_t color565)
{
    for (int y = -r; y <= r; y++) {
        int dx = (int)sqrtf((float)(r * r - y * y));
        for (int x = -dx; x <= dx; x++) {
            put_pixel(cx + x, cy + y, color565);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_circle(int cx, int cy, int r, uint16_t color565)
{
    int x = r, y = 0;
    int p = 1 - r;
    while (x >= y) {
        put_pixel(cx + x, cy + y, color565);
        put_pixel(cx - x, cy + y, color565);
        put_pixel(cx + x, cy - y, color565);
        put_pixel(cx - x, cy - y, color565);
        put_pixel(cx + y, cy + x, color565);
        put_pixel(cx - y, cy + x, color565);
        put_pixel(cx + y, cy - x, color565);
        put_pixel(cx - y, cy - x, color565);
        y++;
        if (p <= 0) { p += 2 * y + 1; }
        else { x--; p += 2 * (y - x) + 1; }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_arc(int cx, int cy, int radius,
                                float start_deg, float end_deg, uint16_t color565)
{
    float start_rad = start_deg * M_PI / 180.0f;
    float end_rad = end_deg * M_PI / 180.0f;
    int steps = (int)(fabsf(end_deg - start_deg) * 2);
    if (steps < 8) steps = 8;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float a = start_rad + t * (end_rad - start_rad);
        int x = cx + (int)(cosf(a) * radius);
        int y = cy + (int)(sinf(a) * radius);
        put_pixel(x, y, color565);
    }
    return ESP_OK;
}

esp_err_t display_hal_fill_arc(int cx, int cy, int inner_r, int outer_r,
                                float start_deg, float end_deg, uint16_t color565)
{
    for (int r = inner_r; r <= outer_r; r++) {
        display_hal_draw_arc(cx, cy, r, start_deg, end_deg, color565);
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_ellipse(int cx, int cy, int rx, int ry, uint16_t color565)
{
    for (float a = 0; a < 360.0f; a += 0.5f) {
        float rad = a * M_PI / 180.0f;
        int x = cx + (int)(cosf(rad) * rx);
        int y = cy + (int)(sinf(rad) * ry);
        put_pixel(x, y, color565);
    }
    return ESP_OK;
}

esp_err_t display_hal_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t color565)
{
    for (int y = -ry; y <= ry; y++) {
        float fy = (float)y / (float)ry;
        float dx = sqrtf(1.0f - fy * fy) * rx;
        for (int x = (int)-dx; x <= (int)dx; x++) {
            put_pixel(cx + x, cy + y, color565);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_round_rect(int x, int y, int w, int h, int radius, uint16_t color565)
{
    /* Top/bottom straight lines */
    for (int i = x + radius; i < x + w - radius; i++) {
        put_pixel(i, y, color565);
        put_pixel(i, y + h - 1, color565);
    }
    /* Left/right straight lines */
    for (int i = y + radius; i < y + h - radius; i++) {
        put_pixel(x, i, color565);
        put_pixel(x + w - 1, i, color565);
    }
    /* Corner arcs */
    int cx_vals[4] = { x + radius, x + w - radius - 1, x + radius, x + w - radius - 1 };
    int cy_vals[4] = { y + radius, y + radius, y + h - radius - 1, y + h - radius - 1 };
    float s[4] = { 180, 270, 90, 0 };
    float e[4] = { 270, 360, 180, 90 };
    for (int c = 0; c < 4; c++) {
        for (float a = s[c]; a <= e[c]; a += 1.0f) {
            float rad = a * M_PI / 180.0f;
            int px = cx_vals[c] + (int)(cosf(rad) * radius);
            int py = cy_vals[c] + (int)(sinf(rad) * radius);
            put_pixel(px, py, color565);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_fill_round_rect(int x, int y, int w, int h, int radius, uint16_t color565)
{
    /* Fill center rectangle */
    for (int by = y + radius; by < y + h - radius; by++) {
        for (int bx = x; bx < x + w; bx++) {
            put_pixel(bx, by, color565);
        }
    }
    /* Fill top and bottom rounded bands */
    for (int dy = 0; dy < radius; dy++) {
        float fy = (float)(radius - dy) / (float)radius;
        int dx = (int)(sqrtf(1.0f - fy * fy) * radius);
        for (int ix = x + radius - dx; ix < x + w - radius + dx; ix++) {
            put_pixel(ix, y + dy, color565);
            put_pixel(ix, y + h - 1 - dy, color565);
        }
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
    /* Sort by y */
    if (y1 > y2) { int t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y1 > y3) { int t; t = x1; x1 = x3; x3 = t; t = y1; y1 = y3; y3 = t; }
    if (y2 > y3) { int t; t = x2; x2 = x3; x3 = t; t = y2; y2 = y3; y3 = t; }

    for (int y = y1; y <= y3; y++) {
        int x_left, x_right;
        if (y < y2) {
            float t = (y2 - y1) ? (float)(y - y1) / (float)(y2 - y1) : 0;
            x_left = x1 + (int)((x2 - x1) * t);
        } else {
            float t = (y3 - y2) ? (float)(y - y2) / (float)(y3 - y2) : 0;
            x_left = x2 + (int)((x3 - x2) * t);
        }
        float t2 = (y3 - y1) ? (float)(y - y1) / (float)(y3 - y1) : 0;
        x_right = x1 + (int)((x3 - x1) * t2);
        if (x_left > x_right) { int tmp = x_left; x_left = x_right; x_right = tmp; }
        for (int x = x_left; x <= x_right; x++) {
            put_pixel(x, y, color565);
        }
    }
    return ESP_OK;
}

/* ---- Text (SDL2_ttf, font-stack fallback for emoji/symbols/CJK) ---- */

static void font_stack_close(void)
{
    for (int i = 0; i < s_font_stack_count; i++) {
        if (s_font_stack[i]) TTF_CloseFont(s_font_stack[i]);
    }
    s_font_stack_count = 0;
    s_font_stack_ptsize = 0;
}

static int font_stack_load(int ptsize)
{
    if (s_font_stack_count > 0 && ptsize == s_font_stack_ptsize)
        return s_font_stack_count;

    glyph_cache_clear();
    font_stack_close();
    s_font_stack_ptsize = ptsize;

    for (int i = 0; i < s_font_path_count; i++) {
        TTF_Font *f = TTF_OpenFont(s_font_paths[i], ptsize);
        if (f) {
            s_font_stack[s_font_stack_count++] = f;
        }
    }
    return s_font_stack_count;
}

static TTF_Font *font_for_codepoint(Uint32 cp)
{
    /* For supplementary-plane characters (real emoji: U+1Fxxx, etc.),
       try the last font (color emoji) first so emojis render in colour
       instead of falling back to a monochrome glyph in an earlier font. */
    if (cp > 0xFFFF && s_font_stack_count > 0) {
        TTF_Font *emoji = s_font_stack[s_font_stack_count - 1];
        if (TTF_GlyphIsProvided32(emoji, cp))
            return emoji;
    }

    for (int i = 0; i < s_font_stack_count; i++) {
        if (TTF_GlyphIsProvided32(s_font_stack[i], cp))
            return s_font_stack[i];
    }
    return s_font_stack_count > 0 ? s_font_stack[0] : NULL;
}

/* Expected line height for the current font-stack point size. */
static int font_stack_line_height(void)
{
    if (s_font_stack_count == 0) return 16;
    return TTF_FontHeight(s_font_stack[0]);
}

/* Decode one UTF-8 codepoint, advance *p past the consumed bytes. */
static Uint32 utf8_decode(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    Uint32 cp;

    if ((s[0] & 0x80) == 0) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = ((Uint32)(s[0] & 0x1F) << 6) | (Uint32)(s[1] & 0x3F);
        *p += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = ((Uint32)(s[0] & 0x0F) << 12)
           | ((Uint32)(s[1] & 0x3F) << 6)
           |  (Uint32)(s[2] & 0x3F);
        *p += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = ((Uint32)(s[0] & 0x07) << 18)
           | ((Uint32)(s[1] & 0x3F) << 12)
           | ((Uint32)(s[2] & 0x3F) << 6)
           |  (Uint32)(s[3] & 0x3F);
        *p += 4;
    } else {
        cp = 0xFFFD;  /* replacement character */
        *p += 1;
    }
    return cp;
}

/* Encode one Unicode codepoint to UTF-8, return byte count. */
static int utf8_encode(Uint32 cp, char *buf)
{
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

static void glyph_cache_get_dir(char *buf, size_t bufsz)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, bufsz, "%s/.esp-agent/glyph_cache", home);
}

static void glyph_cache_clear(void)
{
    pthread_mutex_lock(&s_glyph_cache_mutex);
    for (int i = 0; i < s_glyph_cache_count; i++) {
        if (s_glyph_cache[i].surf) SDL_FreeSurface(s_glyph_cache[i].surf);
    }
    s_glyph_cache_count = 0;
    pthread_mutex_unlock(&s_glyph_cache_mutex);
}

/* Insert without disk I/O (used when loading from disk on startup). */
static void glyph_cache_add_entry(Uint32 cp, int ptsize, int font_idx,
                                   uint16_t color565, SDL_Surface *surf)
{
    if (!surf) return;

    /* Evict LRU entry if cache is full */
    if (s_glyph_cache_count >= GLYPH_CACHE_MAX) {
        int lru = 0;
        for (int i = 1; i < s_glyph_cache_count; i++) {
            if (s_glyph_cache[i].last_used < s_glyph_cache[lru].last_used)
                lru = i;
        }
        SDL_FreeSurface(s_glyph_cache[lru].surf);
        s_glyph_cache[lru] = s_glyph_cache[--s_glyph_cache_count];
    }

    s_glyph_cache[s_glyph_cache_count].codepoint   = cp;
    s_glyph_cache[s_glyph_cache_count].ptsize      = ptsize;
    s_glyph_cache[s_glyph_cache_count].font_idx     = font_idx;
    s_glyph_cache[s_glyph_cache_count].fg_color565  = color565;
    s_glyph_cache[s_glyph_cache_count].surf         = surf;
    s_glyph_cache[s_glyph_cache_count].last_used    = s_glyph_cache_tick++;
    s_glyph_cache_count++;
}

/* Save a single glyph to disk.  File format:
 *   [4 bytes LE] width
 *   [4 bytes LE] height
 *   [w * h * 4 bytes] RGBA32 pixels  */
static void glyph_cache_save(Uint32 cp, int ptsize, int font_idx,
                              uint16_t color565, SDL_Surface *surf)
{
    if (!surf) return;

    char dir[512];
    glyph_cache_get_dir(dir, sizeof(dir));
    mkdir(dir, 0755);

    char path[768];
    snprintf(path, sizeof(path), "%s/%X_%d_%d_%X.cache",
             dir, cp, ptsize, font_idx, (unsigned)color565);

    FILE *f = fopen(path, "wb");
    if (!f) return;

    uint32_t w = (uint32_t)surf->w;
    uint32_t h = (uint32_t)surf->h;
    fwrite(&w, 4, 1, f);
    fwrite(&h, 4, 1, f);

    /* Write row by row because SDL_Surface pitch may differ from w*4 */
    for (int y = 0; y < surf->h; y++) {
        fwrite((uint8_t *)surf->pixels + y * surf->pitch, 4, surf->w, f);
    }
    fclose(f);
}

/* Load a single glyph from disk.  Returns NULL if not found or corrupt. */
static SDL_Surface *glyph_cache_load_from_disk(Uint32 cp, int ptsize,
                                                int font_idx, uint16_t color565)
{
    char dir[512];
    glyph_cache_get_dir(dir, sizeof(dir));

    char path[768];
    snprintf(path, sizeof(path), "%s/%X_%d_%d_%X.cache",
             dir, cp, ptsize, font_idx, (unsigned)color565);

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint32_t w = 0, h = 0;
    if (fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 ||
        w == 0 || h == 0 || w > 4096 || h > 4096) {
        fclose(f);
        return NULL;
    }

    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
        0, (int)w, (int)h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) { fclose(f); return NULL; }

    uint8_t *row = malloc(w * 4);
    if (!row) { SDL_FreeSurface(surf); fclose(f); return NULL; }

    for (uint32_t y = 0; y < h; y++) {
        if (fread(row, 4, w, f) != w) {
            free(row); SDL_FreeSurface(surf); fclose(f); return NULL;
        }
        memcpy((uint8_t *)surf->pixels + y * surf->pitch, row, w * 4);
    }
    free(row);
    fclose(f);

    SDL_SetSurfaceBlendMode(surf, SDL_BLENDMODE_BLEND);
    return surf;
}

/* Scan the disk cache directory and pre-warm the in-memory cache. */
static void glyph_cache_load_all(void)
{
    char dir[512];
    glyph_cache_get_dir(dir, sizeof(dir));
    mkdir(dir, 0755);

    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        unsigned cp_hex, color_hex;
        int ptsize, font_idx;
        if (sscanf(ent->d_name, "%X_%d_%d_%X.cache",
                   &cp_hex, &ptsize, &font_idx, &color_hex) == 4) {
            /* Only load if the font index is still valid for the current
               stack.  Stale entries (from a different font configuration)
               are silently ignored. */
            if (font_idx >= s_font_stack_count) continue;

            SDL_Surface *surf = glyph_cache_load_from_disk(
                (Uint32)cp_hex, ptsize, font_idx, (uint16_t)color_hex);
            if (surf) {
                pthread_mutex_lock(&s_glyph_cache_mutex);
                glyph_cache_add_entry((Uint32)cp_hex, ptsize, font_idx,
                                      (uint16_t)color_hex, surf);
                pthread_mutex_unlock(&s_glyph_cache_mutex);
            }
        }
    }
    closedir(d);
}

static SDL_Surface *glyph_cache_lookup(Uint32 cp, int ptsize,
                                        int font_idx, uint16_t color565)
{
    pthread_mutex_lock(&s_glyph_cache_mutex);
    for (int i = 0; i < s_glyph_cache_count; i++) {
        if (s_glyph_cache[i].codepoint == cp &&
            s_glyph_cache[i].ptsize == ptsize &&
            s_glyph_cache[i].font_idx == font_idx &&
            s_glyph_cache[i].fg_color565 == color565) {
            s_glyph_cache[i].last_used = s_glyph_cache_tick++;
            SDL_Surface *s = s_glyph_cache[i].surf;
            pthread_mutex_unlock(&s_glyph_cache_mutex);
            return s;
        }
    }
    pthread_mutex_unlock(&s_glyph_cache_mutex);
    return NULL;
}

static void glyph_cache_insert(Uint32 cp, int ptsize, int font_idx,
                                uint16_t color565, SDL_Surface *surf)
{
    if (!surf) return;
    pthread_mutex_lock(&s_glyph_cache_mutex);
    glyph_cache_add_entry(cp, ptsize, font_idx, color565, surf);
    pthread_mutex_unlock(&s_glyph_cache_mutex);
    glyph_cache_save(cp, ptsize, font_idx, color565, surf);
}

esp_err_t display_hal_measure_text(const char *text, uint8_t font_size,
                                    uint16_t *out_w, uint16_t *out_h)
{
    if (!text || !out_w || !out_h) return ESP_ERR_INVALID_ARG;

    int ptsize = font_size * 16;
    if (font_stack_load(ptsize) == 0) {
        *out_w = (uint16_t)(strlen(text) * 8 * font_size);
        *out_h = (uint16_t)(16 * font_size);
        return ESP_OK;
    }

    int total_w = 0, max_h = 0;
    const char *p = text;

    while (*p) {
        const char *seg_start = p;
        Uint32 cp = utf8_decode(&p);
        TTF_Font *font = font_for_codepoint(cp);

        /* group consecutive chars that use the same font */
        while (*p) {
            const char *next = p;
            Uint32 next_cp = utf8_decode(&next);
            if (font_for_codepoint(next_cp) != font) break;
            p = next;
        }

        size_t seg_len = p - seg_start;
        char *seg_buf = malloc(seg_len + 1);
        if (!seg_buf) continue;
        memcpy(seg_buf, seg_start, seg_len);
        seg_buf[seg_len] = '\0';

        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, seg_buf, &w, &h) == 0) {
            /* Cap emoji glyph height to line height for layout purposes.
               The draw function scales oversized glyphs down at render time. */
            int line_h = font_stack_line_height();
            if (h > line_h * 3 / 2) {
                w = (int)(w * (float)line_h / (float)h);
                h = line_h;
            }
            total_w += w;
            if (h > max_h) max_h = h;
        }
        free(seg_buf);
    }

    *out_w = (uint16_t)total_w;
    *out_h = (uint16_t)max_h;
    return ESP_OK;
}

esp_err_t display_hal_draw_text(int x, int y, const char *text, uint8_t font_size,
                                 uint16_t text_color, bool has_bg, uint16_t bg_color)
{
    if (!text || !text[0]) return ESP_ERR_INVALID_ARG;

    int ptsize = font_size * 16;
    if (font_stack_load(ptsize) == 0) return ESP_OK;

    int line_h = font_stack_line_height();
    SDL_Color fg = { 0, 0, 0, 255 };
    rgb565_to_rgb(text_color, &fg.r, &fg.g, &fg.b);

    const char *p = text;
    int cx = x;

    while (*p) {
        Uint32 cp = utf8_decode(&p);
        TTF_Font *font = font_for_codepoint(cp);

        /* Find font index for cache key */
        int font_idx = 0;
        for (int i = 0; i < s_font_stack_count; i++) {
            if (s_font_stack[i] == font) { font_idx = i; break; }
        }

        /* Check glyph cache — large emoji bitmaps are expensive to
           re-rasterize every frame, so we keep pre-rendered surfaces. */
        SDL_Surface *glyph = glyph_cache_lookup(cp, ptsize, font_idx,
                                                 text_color);

        if (!glyph) {
            char mb[5];
            int mb_len = utf8_encode(cp, mb);
            mb[mb_len] = '\0';

            glyph = TTF_RenderUTF8_Blended(font, mb, fg);
            if (!glyph) continue;

            /* Noto Color Emoji uses fixed-size bitmaps (~136 px).
               Scale down to the text line height so emoji don't
               dwarf surrounding CJK text. */
            if (glyph->h > line_h * 3 / 2) {
                float scale = (float)line_h / (float)glyph->h;
                int new_w = (int)(glyph->w * scale);
                int new_h = line_h;
                SDL_Surface *scaled = SDL_CreateRGBSurfaceWithFormat(
                    0, new_w, new_h, 32, SDL_PIXELFORMAT_RGBA32);
                if (scaled) {
                    SDL_SetSurfaceBlendMode(glyph, SDL_BLENDMODE_NONE);
                    SDL_BlitScaled(glyph, NULL, scaled, NULL);
                    SDL_FreeSurface(glyph);
                    glyph = scaled;
                }
            }

            SDL_SetSurfaceBlendMode(glyph, SDL_BLENDMODE_BLEND);
            glyph_cache_insert(cp, ptsize, font_idx, text_color, glyph);
        }

        if (has_bg) {
            SDL_Rect bg_rect = { cx, y, glyph->w, glyph->h };
            SDL_FillRect(s_ctx.surface, &bg_rect, bg_color);
        }
        SDL_Rect dst = { cx, y, glyph->w, glyph->h };
        SDL_BlitSurface(glyph, NULL, s_ctx.surface, &dst);
        cx += glyph->w;
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_text_aligned(int x, int y, int w, int h,
                                         const char *text, uint8_t font_size,
                                         uint16_t text_color, bool has_bg,
                                         uint16_t bg_color,
                                         display_hal_text_align_t align,
                                         display_hal_text_valign_t valign)
{
    if (!text) return ESP_ERR_INVALID_ARG;

    uint16_t tw = 0, th = 0;
    display_hal_measure_text(text, font_size, &tw, &th);

    int tx = x, ty = y;

    switch (align) {
    case DISPLAY_HAL_TEXT_ALIGN_CENTER: tx = x + (w - (int)tw) / 2; break;
    case DISPLAY_HAL_TEXT_ALIGN_RIGHT:  tx = x + w - (int)tw; break;
    default: break;
    }

    switch (valign) {
    case DISPLAY_HAL_TEXT_VALIGN_MIDDLE: ty = y + (h - (int)th) / 2; break;
    case DISPLAY_HAL_TEXT_VALIGN_BOTTOM: ty = y + h - (int)th; break;
    default: break;
    }

    return display_hal_draw_text(tx, ty, text, font_size, text_color, has_bg, bg_color);
}

/* ---- Bitmap (RGB565 pixel array) ---- */

esp_err_t display_hal_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!pixels) return ESP_ERR_INVALID_ARG;
    for (int by = 0; by < h; by++) {
        for (int bx = 0; bx < w; bx++) {
            put_pixel(x + bx, y + by, pixels[by * w + bx]);
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_bitmap_crop(int x, int y,
                                        int src_x, int src_y,
                                        int w, int h,
                                        int src_w, int src_h,
                                        const uint16_t *pixels)
{
    if (!pixels) return ESP_ERR_INVALID_ARG;
    for (int by = 0; by < h; by++) {
        for (int bx = 0; bx < w; bx++) {
            int sx = src_x + bx, sy = src_y + by;
            if (sx < src_w && sy < src_h) {
                put_pixel(x + bx, y + by, pixels[sy * src_w + sx]);
            }
        }
    }
    return ESP_OK;
}

esp_err_t display_hal_draw_bitmap_scaled(int x, int y,
                                          const uint16_t *pixels,
                                          int src_w, int src_h,
                                          int scale_w, int scale_h,
                                          int *out_w, int *out_h)
{
    if (!pixels) return ESP_ERR_INVALID_ARG;
    int dw = src_w * scale_w;
    int dh = src_h * scale_h;
    for (int by = 0; by < dh; by++) {
        int sy = by / scale_h;
        for (int bx = 0; bx < dw; bx++) {
            int sx = bx / scale_w;
            put_pixel(x + bx, y + by, pixels[sy * src_w + sx]);
        }
    }
    if (out_w) *out_w = dw;
    if (out_h) *out_h = dh;
    return ESP_OK;
}

/* ---- Input HAL (display_hal_input.h implementations) ---- */

esp_err_t display_hal_poll_input(void)
{
    process_sdl_events();
    return ESP_OK;
}

esp_err_t display_hal_get_mouse_state(int16_t *out_x, int16_t *out_y,
                                       bool *out_left, bool *out_middle,
                                       bool *out_right, int *out_wheel)
{
    if (!out_x || !out_y) return ESP_ERR_INVALID_ARG;
    pthread_mutex_lock(&s_input.mutex);
    *out_x = s_input.mouse.x;
    *out_y = s_input.mouse.y;
    if (out_left)   *out_left   = s_input.mouse.left;
    if (out_middle) *out_middle = s_input.mouse.middle;
    if (out_right)  *out_right  = s_input.mouse.right;
    if (out_wheel)  *out_wheel  = s_input.mouse.wheel;
    pthread_mutex_unlock(&s_input.mutex);
    return ESP_OK;
}

bool display_hal_is_key_down(int32_t scancode)
{
    if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) return false;
    bool down;
    pthread_mutex_lock(&s_input.mutex);
    down = (s_input.keys[scancode] != 0);
    pthread_mutex_unlock(&s_input.mutex);
    return down;
}

uint16_t display_hal_get_modifiers(void)
{
    uint16_t mod;
    pthread_mutex_lock(&s_input.mutex);
    mod = s_input.modifiers;
    pthread_mutex_unlock(&s_input.mutex);
    return mod;
}

bool display_hal_pop_input_event(input_event_t *out_event)
{
    if (!out_event) return false;
    pthread_mutex_lock(&s_input.mutex);
    if (s_input.queue_count == 0) {
        pthread_mutex_unlock(&s_input.mutex);
        return false;
    }
    *out_event = s_input.queue[s_input.queue_head];
    s_input.queue_head = (s_input.queue_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    s_input.queue_count--;
    pthread_mutex_unlock(&s_input.mutex);
    return true;
}

/* ---- esp_lcd_touch SDL bridge (makes gfx_touch.c work) ---- */

static esp_lcd_touch_handle_t s_touch_sdl = NULL;

esp_lcd_touch_handle_t esp_lcd_touch_init_sdl(void)
{
    if (s_touch_sdl) return s_touch_sdl;
    s_touch_sdl = calloc(1, sizeof(esp_lcd_touch_dev_t));
    if (!s_touch_sdl) return NULL;
    s_touch_sdl->config.int_gpio_num = GPIO_NUM_NC;
    s_touch_sdl->has_data = false;
    return s_touch_sdl;
}

void esp_lcd_touch_feed_sdl(int16_t x, int16_t y, bool pressed)
{
    if (!s_touch_sdl) return;
    s_touch_sdl->last_point.x = x;
    s_touch_sdl->last_point.y = y;
    s_touch_sdl->last_point.strength = pressed ? 128 : 0;
    s_touch_sdl->last_point.track_id = 0;
    s_touch_sdl->has_data = pressed;
    if (!pressed) s_touch_sdl->last_read_ms = 0;
}

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp)
{
    if (!tp) return ESP_ERR_INVALID_ARG;
    /* Data is pushed from SDL event loop — just mark as read. */
    tp->last_read_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return ESP_OK;
}

esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp,
    esp_lcd_touch_point_data_t *out, uint8_t *count, uint16_t max)
{
    if (!tp || !out || !count) return ESP_ERR_INVALID_ARG;
    if (max == 0) { *count = 0; return ESP_OK; }
    if (tp->has_data) {
        out[0] = tp->last_point;
        *count = 1;
    } else {
        *count = 0;
    }
    return ESP_OK;
}

esp_err_t esp_lcd_touch_register_interrupt_callback(
    esp_lcd_touch_handle_t tp, void (*cb)(void *))
{
    if (!tp) return ESP_ERR_INVALID_ARG;
    tp->isr_cb = cb;
    tp->isr_arg = NULL;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(
    esp_lcd_touch_handle_t tp, void (*cb)(void *), void *arg)
{
    if (!tp) return ESP_ERR_INVALID_ARG;
    tp->isr_cb = cb;
    tp->isr_arg = arg;
    return ESP_OK;
}

/* ---- JPEG (stub - real impl would use libjpeg) ---- */

esp_err_t display_hal_draw_jpeg(int x, int y, const uint8_t *data, size_t len,
                                 int *out_w, int *out_h)
{
    (void)x; (void)y; (void)data; (void)len;
    ESP_LOGW(TAG, "JPEG decode not implemented in SDL2 adapter");
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t display_hal_draw_jpeg_crop(int x, int y, int src_x, int src_y,
                                      int w, int h,
                                      const uint8_t *data, size_t len,
                                      int *out_w, int *out_h)
{
    (void)x; (void)y; (void)src_x; (void)src_y; (void)w; (void)h; (void)data; (void)len;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t display_hal_jpeg_get_size(const uint8_t *data, size_t len,
                                     int *out_w, int *out_h)
{
    (void)data; (void)len;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t display_hal_draw_jpeg_scaled(int x, int y,
                                        const uint8_t *data, size_t len,
                                        int scale_w, int scale_h,
                                        int *out_w, int *out_h)
{
    (void)x; (void)y; (void)data; (void)len; (void)scale_w; (void)scale_h;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
