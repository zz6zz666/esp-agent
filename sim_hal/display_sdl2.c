/*
 * display_sdl2.c — SDL2 implementation of display_hal.h for desktop simulator
 *
 * All drawing uses a back-buffer SDL_Surface in RGB565 (matching ESP LCD format).
 * On present(), the surface is converted to an SDL_Texture and rendered.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display_hal.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "display_sdl2";

/* ---- Internal state ---- */
typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    SDL_Surface  *surface;    /* RGB565 back-buffer */
    TTF_Font     *font;       /* TTF font for text rendering */
    int width;
    int height;
    bool frame_active;
    int clip_x, clip_y, clip_w, clip_h;
    bool clip_enabled;
} sdl_ctx_t;

static sdl_ctx_t s_ctx = {0};
static pthread_t s_main_thread;
static pthread_mutex_t s_present_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_present_cond = PTHREAD_COND_INITIALIZER;
static volatile bool g_present_pending = false;
static bool s_ttf_ok = false;

/* forward declarations */
esp_err_t display_hal_destroy(void);
static volatile bool g_display_quit_requested = false;

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
    if (x < 0 || x >= s_ctx.width || y < 0 || y >= s_ctx.height) return;
    if (s_ctx.clip_enabled) {
        if (x < s_ctx.clip_x || x >= s_ctx.clip_x + s_ctx.clip_w ||
            y < s_ctx.clip_y || y >= s_ctx.clip_y + s_ctx.clip_h) return;
    }
    uint16_t *pixels = (uint16_t *)s_ctx.surface->pixels;
    pixels[y * s_ctx.width + x] = color;
}

/* ---- Lifecycle ---- */

esp_err_t display_hal_create(esp_lcd_panel_handle_t panel_handle,
                             esp_lcd_panel_io_handle_t io_handle,
                             display_hal_panel_if_t panel_if,
                             int lcd_width, int lcd_height)
{
    (void)panel_handle; (void)io_handle; (void)panel_if;

    if (s_ctx.window) {
        if (s_ctx.width == lcd_width && s_ctx.height == lcd_height) {
            return ESP_OK;
        }
        display_hal_destroy();
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        ESP_LOGE(TAG, "SDL_Init failed: %s", SDL_GetError());
        return ESP_FAIL;
    }

    s_ctx.width = lcd_width > 0 ? lcd_width : 320;
    s_ctx.height = lcd_height > 0 ? lcd_height : 240;

    s_ctx.window = SDL_CreateWindow("esp-claw Desktop Simulator",
                                     SDL_WINDOWPOS_UNDEFINED,
                                     SDL_WINDOWPOS_UNDEFINED,
                                     s_ctx.width * 2,  /* 2x scale */
                                     s_ctx.height * 2,
                                     SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!s_ctx.window) {
        ESP_LOGE(TAG, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return ESP_FAIL;
    }

    s_ctx.renderer = SDL_CreateRenderer(s_ctx.window, -1,
                                         SDL_RENDERER_ACCELERATED);
    if (!s_ctx.renderer) {
        ESP_LOGE(TAG, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(s_ctx.window);
        SDL_Quit();
        return ESP_FAIL;
    }
    SDL_SetRenderDrawColor(s_ctx.renderer, 0, 0, 0, 255);

    /* RGB565 surface as back-buffer */
    s_ctx.surface = SDL_CreateRGBSurfaceWithFormat(0, s_ctx.width, s_ctx.height,
                                                    16, SDL_PIXELFORMAT_RGB565);
    if (!s_ctx.surface) {
        ESP_LOGE(TAG, "SDL_CreateRGBSurface failed: %s", SDL_GetError());
        SDL_DestroyRenderer(s_ctx.renderer);
        SDL_DestroyWindow(s_ctx.window);
        SDL_Quit();
        return ESP_FAIL;
    }

    s_ctx.texture = SDL_CreateTexture(s_ctx.renderer,
                                       SDL_PIXELFORMAT_RGB565,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       s_ctx.width, s_ctx.height);
    if (!s_ctx.texture) {
        ESP_LOGE(TAG, "SDL_CreateTexture failed: %s", SDL_GetError());
        SDL_FreeSurface(s_ctx.surface);
        SDL_DestroyRenderer(s_ctx.renderer);
        SDL_DestroyWindow(s_ctx.window);
        SDL_Quit();
        return ESP_FAIL;
    }

    /* TTF font for text rendering (Unicode / Chinese support) */
    if (TTF_Init() < 0) {
        ESP_LOGW(TAG, "TTF_Init failed: %s", TTF_GetError());
        s_ttf_ok = false;
    } else {
        s_ctx.font = TTF_OpenFont("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", 16);
        if (!s_ctx.font) {
            ESP_LOGW(TAG, "TTF_OpenFont failed: %s — text will not render", TTF_GetError());
            s_ttf_ok = false;
        } else {
            ESP_LOGI(TAG, "TTF font loaded: WQY ZenHei");
            s_ttf_ok = true;
        }
    }

    s_main_thread = pthread_self();
    ESP_LOGI(TAG, "Display created: %dx%d (main_thread=%lu)",
             s_ctx.width, s_ctx.height, (unsigned long)s_main_thread);
    return ESP_OK;
}

esp_err_t display_hal_destroy(void)
{
    /* SDL2 window, renderer, texture, and surface are created once at
       startup on the main thread and live for the entire process lifetime.
       Destroying them from Lua's disp.deinit() would tear down resources
       that the main loop and emote engine still depend on. */
    return ESP_OK;
}

/* ---- Geometry ---- */

int display_hal_width(void)  { return s_ctx.width; }
int display_hal_height(void) { return s_ctx.height; }

/* ---- Frame control ---- */

esp_err_t display_hal_begin_frame(bool clear, uint16_t color565)
{
    if (clear) {
        SDL_FillRect(s_ctx.surface, NULL, color565);
    }
    s_ctx.frame_active = true;
    return ESP_OK;
}

esp_err_t display_hal_present(void)
{
    if (!s_ctx.texture || !s_ctx.surface) return ESP_ERR_INVALID_STATE;

    if (pthread_equal(pthread_self(), s_main_thread)) {
        /* Main thread: execute SDL2 GPU rendering (always, no gate).
           If a worker thread is waiting on g_present_pending, signal it. */
        pthread_mutex_lock(&s_present_mutex);
        SDL_UpdateTexture(s_ctx.texture, NULL, s_ctx.surface->pixels, s_ctx.width * 2);
        SDL_RenderClear(s_ctx.renderer);
        SDL_RenderCopy(s_ctx.renderer, s_ctx.texture, NULL, NULL);
        SDL_RenderPresent(s_ctx.renderer);
        g_present_pending = false;
        pthread_cond_broadcast(&s_present_cond);
        pthread_mutex_unlock(&s_present_mutex);

        /* Pump events so the window stays responsive */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                ESP_LOGI(TAG, "Window closed by user");
                g_display_quit_requested = true;
                return ESP_OK;
            }
        }
    } else {
        /* Worker thread (Lua): signal that a frame is ready and block
           until the main loop renders it.  This prevents the Lua script
           from finishing and releasing the display arbiter before the
           main loop has had a chance to present the frame. */
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
        (uint8_t *)s_ctx.surface->pixels + (y * s_ctx.width + x) * 2,
        width, height, 16, s_ctx.width * 2, SDL_PIXELFORMAT_RGB565);
    if (!sub) return ESP_ERR_NO_MEM;
    SDL_UpdateTexture(s_ctx.texture, &r, sub->pixels, sub->pitch);
    SDL_RenderClear(s_ctx.renderer);
    SDL_RenderCopy(s_ctx.renderer, s_ctx.texture, NULL, NULL);
    SDL_RenderPresent(s_ctx.renderer);
    SDL_FreeSurface(sub);

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            ESP_LOGI(TAG, "Window closed by user");
            g_display_quit_requested = true;
            return ESP_OK;
        }
    }
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

/* ---- Text (SDL2_ttf, WQY ZenHei) ---- */

static TTF_Font *get_font(int ptsize)
{
    static int cached_ptsize = 0;
    if (!s_ttf_ok) return NULL;
    if (s_ctx.font && ptsize == cached_ptsize) return s_ctx.font;
    if (s_ctx.font) TTF_CloseFont(s_ctx.font);
    s_ctx.font = TTF_OpenFont("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc", ptsize);
    cached_ptsize = ptsize;
    return s_ctx.font;
}

esp_err_t display_hal_measure_text(const char *text, uint8_t font_size,
                                    uint16_t *out_w, uint16_t *out_h)
{
    if (!text || !out_w || !out_h) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.font && !get_font(16)) {
        *out_w = (uint16_t)(strlen(text) * 8 * font_size);
        *out_h = (uint16_t)(16 * font_size);
        return ESP_OK;
    }
    TTF_Font *f = get_font(font_size * 16);
    if (!f) return ESP_ERR_INVALID_STATE;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(f, text, &w, &h) < 0) return ESP_ERR_INVALID_STATE;
    *out_w = (uint16_t)w;
    *out_h = (uint16_t)h;
    return ESP_OK;
}

esp_err_t display_hal_draw_text(int x, int y, const char *text, uint8_t font_size,
                                 uint16_t text_color, bool has_bg, uint16_t bg_color)
{
    if (!text || !text[0]) return ESP_ERR_INVALID_ARG;
    if (!s_ctx.font && !get_font(16)) {
        /* Fallback: simple pixel (no TTF available) */
        return ESP_OK;
    }

    TTF_Font *f = get_font(font_size * 16);
    if (!f) return ESP_ERR_INVALID_STATE;

    SDL_Color fg = { 0, 0, 0, 255 };
    rgb565_to_rgb(text_color, &fg.r, &fg.g, &fg.b);

    SDL_Surface *glyph = TTF_RenderUTF8_Blended(f, text, fg);
    if (!glyph) return ESP_ERR_INVALID_STATE;

    if (has_bg) {
        SDL_Rect bg_rect = { x, y, glyph->w, glyph->h };
        SDL_FillRect(s_ctx.surface, &bg_rect, bg_color);
    }

    /* Let SDL handle format conversion and alpha blending onto the
       RGB565 back-buffer — avoids all per-pixel format/endian issues. */
    SDL_SetSurfaceBlendMode(glyph, SDL_BLENDMODE_BLEND);
    SDL_Rect dst = { x, y, glyph->w, glyph->h };
    SDL_BlitSurface(glyph, NULL, s_ctx.surface, &dst);

    SDL_FreeSurface(glyph);
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
