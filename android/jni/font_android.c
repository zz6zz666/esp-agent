/*
 * font_android.c — Android TrueType font rendering via stb_truetype
 *
 * Architecture mirrors display_sdl2.c glyph cache and font stack.
 * Uses stb_truetype (single-header) instead of SDL2_ttf.
 * Fonts discovered from /system/fonts/ + {data_dir}/fonts/.
 */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

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
#include <unistd.h>

#include "display_hal.h"
#include "esp_err.h"
#include "esp_log.h"
#include "font_android.h"

/* JNI text rendering fallback (uses Android native font engine for CJK) */
extern uint8_t *display_android_render_text(const char *text, int font_size,
                                              int *out_w, int *out_h);

static const char *TAG = "font_android";

/* ---- Font descriptor ---- */
typedef struct {
    stbtt_fontinfo  info;
    uint8_t        *font_data;      /* owned, loaded from disk or assets */
    size_t          font_size;
    char           *path;           /* strdup'd file path */
    const char     *label;          /* human-readable label (static string) */
    int             loaded_ptsize;  /* ptsize this font is currently scaled for */
    float           scale;          /* stbtt_ScaleForPixelHeight(loaded_ptsize) */
    int             ascent;         /* cached vertical metrics */
    int             descent;
    int             line_gap;
} android_font_t;

/* ---- Font stack ---- */
static android_font_t s_font_stack[FA_MAX_FONT_STACK];
static int             s_font_stack_count = 0;
static int             s_font_stack_ptsize = 0;

/* ---- Font paths discovered on startup ---- */
static char    *s_font_paths[FA_MAX_FONT_STACK];
static const char *s_font_labels[FA_MAX_FONT_STACK];
static int      s_font_path_count = 0;

/* ---- Glyph bitmap (RGBA32) ---- */
typedef struct {
    int      w, h;
    uint8_t *pixels;     /* RGBA32, row-major, must be freed */
} glyph_bitmap_t;

/* ---- Glyph cache ---- */
typedef struct {
    uint32_t       codepoint;
    int            ptsize;
    int            font_idx;
    uint16_t       fg_color565;
    glyph_bitmap_t bitmap;
    int            last_used;
} glyph_cache_entry_t;

static glyph_cache_entry_t s_glyph_cache[FA_GLYPH_CACHE_MAX];
static int                 s_glyph_cache_count = 0;
static int                 s_glyph_cache_tick  = 0;
static pthread_mutex_t     s_glyph_cache_mutex;  /* recursive: held during draw calls */
static pthread_mutex_t     s_glyph_disk_mutex  = PTHREAD_MUTEX_INITIALIZER;

static char s_glyph_cache_dir[512];
static bool s_glyph_cache_dir_set = false;

/* ---- Forward declarations ---- */
static void font_stack_close(void);
static int  font_stack_load(int ptsize);
static android_font_t *font_for_codepoint(uint32_t cp);
static int  font_stack_line_height(void);
static uint32_t utf8_decode(const char **p);
static int  utf8_encode(uint32_t cp, char *buf);
static glyph_bitmap_t *glyph_cache_lookup(uint32_t cp, int ptsize, int font_idx, uint16_t color565);
static void glyph_cache_insert(uint32_t cp, int ptsize, int font_idx, uint16_t color565, glyph_bitmap_t *bitmap);
static void glyph_cache_add_entry(uint32_t cp, int ptsize, int font_idx, uint16_t color565, glyph_bitmap_t *bitmap);
static void glyph_cache_save(uint32_t cp, int ptsize, int font_idx, uint16_t color565, glyph_bitmap_t *bitmap);
static void glyph_cache_load_all(void);
static void glyph_cache_clear(void);
static glyph_bitmap_t *render_glyph(uint32_t cp, int ptsize, android_font_t *font);
static void free_glyph_bitmap(glyph_bitmap_t *b);

/* Render a glyph using Android's native font engine (JNI).
 * Handles CJK, complex scripts, and any characters that stb_truetype can't. */
static glyph_bitmap_t *render_glyph_jni(uint32_t cp, int ptsize)
{
    char mb[5];
    int mb_len = utf8_encode(cp, mb);
    mb[mb_len] = '\0';

    int jw = 0, jh = 0;
    uint8_t *jni_data = display_android_render_text(mb, ptsize, &jw, &jh);
    if (!jni_data || jw <= 0 || jh <= 0) {
        if (jni_data) free(jni_data);
        return NULL;
    }

    glyph_bitmap_t *b = calloc(1, sizeof(*b));
    if (!b) { free(jni_data); return NULL; }

    b->w = jw;
    b->h = jh;
    b->pixels = jni_data;  /* jni_data is already RGBA32, take ownership */
    return b;
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

/* ---- Font file loading ---- */
static uint8_t *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

/* ---- Font discovery ---- */

static void try_font_path(const char *dir, const char *name, const char *label)
{
    if (s_font_path_count >= FA_MAX_FONT_STACK) return;
    char full[512];
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    if (access(full, F_OK) == 0) {
        s_font_paths[s_font_path_count] = strdup(full);
        s_font_labels[s_font_path_count] = label;
        ESP_LOGI(TAG, "Font discovered: %s (%s)", full, label);
        s_font_path_count++;
    }
}

static void discover_fonts_android(const char *data_dir)
{
    s_font_path_count = 0;

    /* 1. Android system fonts */
    try_font_path("/system/fonts", "Roboto-Regular.ttf",    "Roboto");
    try_font_path("/system/fonts", "HYQiHei_60S.ttf",       "Han Yi Qi Hei");
    try_font_path("/system/fonts", "HYQiHei_70S.ttf",       "Han Yi Qi Hei");
    try_font_path("/system/fonts", "NotoColorEmoji.ttf",    "Noto Color Emoji");
    try_font_path("/system/fonts", "NotoSansCJK-Regular.ttc","Noto Sans CJK");
    try_font_path("/system/fonts", "DroidSansFallback.ttf", "Droid Sans Fallback");
    try_font_path("/system/fonts", "NotoSansSC-Regular.ttf", "Noto Sans SC");

    if (s_font_path_count == 0) {
        try_font_path("/system/fonts", "DroidSans.ttf",     "Droid Sans");
    }

    /* 2. Bundled fonts (extracted to data_dir/fonts/) */
    if (data_dir) {
        char font_dir[512];
        snprintf(font_dir, sizeof(font_dir), "%s/fonts", data_dir);
        try_font_path(font_dir, "Roboto-Regular.ttf",    "Roboto");
        try_font_path(font_dir, "NotoSansSC-Regular.ttf", "Noto Sans SC");
        try_font_path(font_dir, "NotoColorEmoji.ttf",    "Noto Color Emoji");
    }

    ESP_LOGI(TAG, "Font discovery: %d fonts found", s_font_path_count);
}

/* ---- Font stack lifecycle ---- */

static void font_stack_close(void)
{
    for (int i = 0; i < s_font_stack_count; i++) {
        if (s_font_stack[i].font_data) {
            free(s_font_stack[i].font_data);
            s_font_stack[i].font_data = NULL;
        }
        memset(&s_font_stack[i], 0, sizeof(android_font_t));
    }
    s_font_stack_count = 0;
    s_font_stack_ptsize = 0;
}

static int font_stack_load(int ptsize)
{
    if (s_font_stack_count > 0 && ptsize == s_font_stack_ptsize)
        return s_font_stack_count;

    font_stack_close();
    s_font_stack_ptsize = ptsize;

    for (int i = 0; i < s_font_path_count; i++) {
        size_t sz = 0;
        uint8_t *data = load_file(s_font_paths[i], &sz);
        if (!data) {
            ESP_LOGW(TAG, "Failed to load font file: %s", s_font_paths[i]);
            continue;
        }
        ESP_LOGI(TAG, "Font file loaded: %s (%zu bytes)", s_font_paths[i], sz);

        android_font_t *f = &s_font_stack[s_font_stack_count];
        memset(f, 0, sizeof(*f));

        int offset = stbtt_GetFontOffsetForIndex(data, 0);
        if (offset < 0) offset = 0;
        if (!stbtt_InitFont(&f->info, data, offset)) {
            free(data);
            ESP_LOGW(TAG, "stbtt_InitFont failed (offset=%d) for %s "
                     "(likely CFF/OTF format or corrupt file)", offset, s_font_paths[i]);
            continue;
        }

        f->font_data = data;
        f->font_size = sz;
        f->path = s_font_paths[i];
        f->label = s_font_labels[i];
        f->loaded_ptsize = ptsize;
        f->scale = stbtt_ScaleForPixelHeight(&f->info, (float)ptsize);

        stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->line_gap);

        ESP_LOGI(TAG, "Font loaded: %s (%s) ptsize=%d scale=%.3f",
                 f->label, f->path, ptsize, (double)f->scale);
        s_font_stack_count++;
    }

    return s_font_stack_count;
}

static android_font_t *font_for_codepoint(uint32_t cp)
{
    if (s_font_stack_count == 0) return NULL;

    /* For supplementary-plane characters (emoji U+1Fxxx+), try last font first */
    if (cp > 0xFFFF && s_font_stack_count > 1) {
        android_font_t *emoji = &s_font_stack[s_font_stack_count - 1];
        if (stbtt_FindGlyphIndex(&emoji->info, (int)cp) != 0)
            return emoji;
    }

    for (int i = 0; i < s_font_stack_count; i++) {
        if (stbtt_FindGlyphIndex(&s_font_stack[i].info, (int)cp) != 0)
            return &s_font_stack[i];
    }

    return &s_font_stack[0];
}

static int font_stack_line_height(void)
{
    if (s_font_stack_count == 0) return 16;
    android_font_t *f = &s_font_stack[0];
    int height = (int)((f->ascent - f->descent + f->line_gap) * f->scale + 0.5f);
    return height > 0 ? height : 16;
}

/* ---- UTF-8 helpers ---- */

static uint32_t utf8_decode(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;

    if ((s[0] & 0x80) == 0) {
        cp = s[0]; *p += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *p += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12)
           | ((uint32_t)(s[1] & 0x3F) << 6)
           |  (uint32_t)(s[2] & 0x3F);
        *p += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = ((uint32_t)(s[0] & 0x07) << 18)
           | ((uint32_t)(s[1] & 0x3F) << 12)
           | ((uint32_t)(s[2] & 0x3F) << 6)
           |  (uint32_t)(s[3] & 0x3F);
        *p += 4;
    } else {
        cp = 0xFFFD; *p += 1;
    }
    return cp;
}

static int utf8_encode(uint32_t cp, char *buf)
{
    if (cp < 0x80) {
        buf[0] = (char)cp; return 1;
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

/* ---- Glyph rendering with stb_truetype ---- */

static glyph_bitmap_t *render_glyph(uint32_t cp, int ptsize, android_font_t *font)
{
    if (!font || font->loaded_ptsize != ptsize) return NULL;

    float scale = font->scale;
    int w, h, xoff, yoff;
    unsigned char *grey = stbtt_GetCodepointBitmap(
        &font->info, scale, scale, (int)cp, &w, &h, &xoff, &yoff);

    if (!grey || w == 0 || h == 0) {
        if (grey) stbtt_FreeBitmap(grey, NULL);
        return NULL;
    }

    glyph_bitmap_t *b = calloc(1, sizeof(*b));
    if (!b) { stbtt_FreeBitmap(grey, NULL); return NULL; }

    b->w = w;
    b->h = h;
    b->pixels = malloc((size_t)w * h * 4);
    if (!b->pixels) {
        free(b);
        stbtt_FreeBitmap(grey, NULL);
        return NULL;
    }

    /* Convert greyscale to white RGBA32 (alpha = glyph opacity) */
    for (int i = 0; i < w * h; i++) {
        b->pixels[i * 4 + 0] = 255;
        b->pixels[i * 4 + 1] = 255;
        b->pixels[i * 4 + 2] = 255;
        b->pixels[i * 4 + 3] = grey[i];
    }

    stbtt_FreeBitmap(grey, NULL);
    return b;
}

static void free_glyph_bitmap(glyph_bitmap_t *b)
{
    if (!b) return;
    if (b->pixels) free(b->pixels);
    free(b);
}

/* ---- Glyph cache ---- */

static void glyph_cache_add_entry(uint32_t cp, int ptsize, int font_idx,
                                   uint16_t color565, glyph_bitmap_t *bitmap)
{
    if (!bitmap) return;

    if (s_glyph_cache_count >= FA_GLYPH_CACHE_MAX) {
        int lru = 0;
        for (int i = 1; i < s_glyph_cache_count; i++) {
            if (s_glyph_cache[i].last_used < s_glyph_cache[lru].last_used)
                lru = i;
        }
        free_glyph_bitmap(&s_glyph_cache[lru].bitmap);
        s_glyph_cache[lru] = s_glyph_cache[--s_glyph_cache_count];
    }

    s_glyph_cache[s_glyph_cache_count].codepoint   = cp;
    s_glyph_cache[s_glyph_cache_count].ptsize      = ptsize;
    s_glyph_cache[s_glyph_cache_count].font_idx     = font_idx;
    s_glyph_cache[s_glyph_cache_count].fg_color565  = color565;
    s_glyph_cache[s_glyph_cache_count].bitmap       = *bitmap;
    s_glyph_cache[s_glyph_cache_count].last_used    = s_glyph_cache_tick++;
    s_glyph_cache_count++;
}

static void glyph_cache_save(uint32_t cp, int ptsize, int font_idx,
                              uint16_t color565, glyph_bitmap_t *bitmap)
{
    if (!bitmap || !s_glyph_cache_dir_set) return;

    mkdir(s_glyph_cache_dir, 0755);

    char path[768];
    snprintf(path, sizeof(path), "%s/glyph_cache.bin", s_glyph_cache_dir);

    pthread_mutex_lock(&s_glyph_disk_mutex);

    uint32_t count = 0;
    FILE *f = fopen(path, "rb+");
    if (f) {
        uint32_t magic;
        if (fread(&magic, 4, 1, f) == 1 && magic == FA_GLYPH_CACHE_MAGIC) {
            fread(&count, 4, 1, f);
        }
        rewind(f);
        magic = FA_GLYPH_CACHE_MAGIC;
        count++;
        fwrite(&magic, 4, 1, f);
        fwrite(&count, 4, 1, f);
        fseek(f, 0, SEEK_END);
    } else {
        f = fopen(path, "wb");
        if (!f) { pthread_mutex_unlock(&s_glyph_disk_mutex); return; }
        uint32_t magic = FA_GLYPH_CACHE_MAGIC;
        count = 1;
        fwrite(&magic, 4, 1, f);
        fwrite(&count, 4, 1, f);
    }

    uint32_t w32 = (uint32_t)bitmap->w;
    uint32_t h32 = (uint32_t)bitmap->h;
    uint32_t cp32 = cp;
    uint32_t pt32 = (uint32_t)ptsize;
    uint32_t fi32 = (uint32_t)font_idx;
    fwrite(&cp32, 4, 1, f);
    fwrite(&pt32, 4, 1, f);
    fwrite(&fi32, 4, 1, f);
    fwrite(&color565, 2, 1, f);
    fwrite(&w32, 4, 1, f);
    fwrite(&h32, 4, 1, f);
    fwrite(bitmap->pixels, 4, (size_t)w32 * h32, f);
    fclose(f);

    pthread_mutex_unlock(&s_glyph_disk_mutex);
}

static void glyph_cache_load_all(void)
{
    if (!s_glyph_cache_dir_set) return;

    char path[768];
    snprintf(path, sizeof(path), "%s/glyph_cache.bin", s_glyph_cache_dir);

    FILE *f = fopen(path, "rb");
    if (!f) return;

    uint32_t magic, count;
    if (fread(&magic, 4, 1, f) != 1 || magic != FA_GLYPH_CACHE_MAGIC ||
        fread(&count, 4, 1, f) != 1 || count == 0) {
        fclose(f);
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t cp32, pt32, fi32, w32, h32;
        uint16_t color565;
        if (fread(&cp32, 4, 1, f) != 1 ||
            fread(&pt32, 4, 1, f) != 1 ||
            fread(&fi32, 4, 1, f) != 1 ||
            fread(&color565, 2, 1, f) != 1 ||
            fread(&w32, 4, 1, f) != 1 ||
            fread(&h32, 4, 1, f) != 1) break;

        if (w32 == 0 || h32 == 0 || w32 > 4096 || h32 > 4096) {
            fseek(f, (long)w32 * h32 * 4, SEEK_CUR); continue;
        }
        if ((int)fi32 >= s_font_stack_count) {
            fseek(f, (long)w32 * h32 * 4, SEEK_CUR); continue;
        }

        glyph_bitmap_t bm = {0};
        bm.w = (int)w32;
        bm.h = (int)h32;
        bm.pixels = malloc((size_t)w32 * h32 * 4);
        if (!bm.pixels) {
            fseek(f, (long)w32 * h32 * 4, SEEK_CUR); continue;
        }
        if (fread(bm.pixels, 4, (size_t)w32 * h32, f) != (size_t)w32 * h32) {
            free(bm.pixels);
            break;
        }

        pthread_mutex_lock(&s_glyph_cache_mutex);
        glyph_cache_add_entry(cp32, (int)pt32, (int)fi32, color565, &bm);
        pthread_mutex_unlock(&s_glyph_cache_mutex);
    }

    fclose(f);
    ESP_LOGI(TAG, "Glyph cache loaded: %d entries", s_glyph_cache_count);
}

static void glyph_cache_clear(void)
{
    pthread_mutex_lock(&s_glyph_cache_mutex);
    for (int i = 0; i < s_glyph_cache_count; i++) {
        free_glyph_bitmap(&s_glyph_cache[i].bitmap);
    }
    s_glyph_cache_count = 0;
    pthread_mutex_unlock(&s_glyph_cache_mutex);
}

static glyph_bitmap_t *glyph_cache_lookup(uint32_t cp, int ptsize,
                                           int font_idx, uint16_t color565)
{
    pthread_mutex_lock(&s_glyph_cache_mutex);
    for (int i = 0; i < s_glyph_cache_count; i++) {
        if (s_glyph_cache[i].codepoint == cp &&
            s_glyph_cache[i].ptsize == ptsize &&
            s_glyph_cache[i].font_idx == font_idx &&
            s_glyph_cache[i].fg_color565 == color565) {
            s_glyph_cache[i].last_used = s_glyph_cache_tick++;
            glyph_bitmap_t *b = &s_glyph_cache[i].bitmap;
            pthread_mutex_unlock(&s_glyph_cache_mutex);
            return b;
        }
    }
    pthread_mutex_unlock(&s_glyph_cache_mutex);
    return NULL;
}

static void glyph_cache_insert(uint32_t cp, int ptsize, int font_idx,
                                uint16_t color565, glyph_bitmap_t *bitmap)
{
    if (!bitmap) return;
    pthread_mutex_lock(&s_glyph_cache_mutex);
    glyph_cache_add_entry(cp, ptsize, font_idx, color565, bitmap);
    pthread_mutex_unlock(&s_glyph_cache_mutex);
}

/* ---- Public API: measure text ---- */

esp_err_t font_android_measure_text(const char *text, uint8_t font_size,
                                    uint16_t *out_w, uint16_t *out_h)
{
    if (!text || !out_w || !out_h) return ESP_ERR_INVALID_ARG;

    /* Measure cache: avoid expensive per-glyph metric queries for static text */
#define MEASURE_CACHE_MAX 32
    typedef struct {
        uint32_t hash;
        uint8_t  font_size;
        uint16_t w, h;
    } measure_cache_t;
    static measure_cache_t s_measure_cache[MEASURE_CACHE_MAX];
    static int             s_measure_cache_count = 0;

    /* djb2 hash */
    uint32_t hash = 5381;
    for (const char *s = text; *s; s++) hash = ((hash << 5) + hash) + (unsigned char)*s;

    for (int i = 0; i < s_measure_cache_count; i++) {
        if (s_measure_cache[i].hash == hash &&
            s_measure_cache[i].font_size == font_size) {
            *out_w = s_measure_cache[i].w;
            *out_h = s_measure_cache[i].h;
            return ESP_OK;
        }
    }

    int ptsize = font_size;
    if (font_stack_load(ptsize) == 0) {
        *out_w = (uint16_t)(strlen(text) * 8 * font_size);
        *out_h = (uint16_t)(16 * font_size);
        return ESP_OK;
    }

    int total_w = 0, max_h = 0;
    int line_h = font_stack_line_height();
    const char *p = text;

    while (*p) {
        const char *seg_start = p;
        uint32_t cp = utf8_decode(&p);
        android_font_t *font = font_for_codepoint(cp);

        /* Group consecutive chars using the same font */
        while (*p) {
            const char *next = p;
            uint32_t next_cp = utf8_decode(&next);
            if (font_for_codepoint(next_cp) != font) break;
            p = next;
        }

        size_t seg_len = p - seg_start;
        char *seg_buf = malloc(seg_len + 1);
        if (!seg_buf) continue;
        memcpy(seg_buf, seg_start, seg_len);
        seg_buf[seg_len] = '\0';

        /* Measure each codepoint in the segment */
        int seg_w = 0;
        const char *sp = seg_buf;
        while (*sp) {
            uint32_t ccp = utf8_decode(&sp);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font->info, (int)ccp, &adv, &lsb);
            int gw = (int)(adv * font->scale + 0.5f);
            if (gw > 0) seg_w += gw;
        }

        /* Cap oversized glyphs to line height */
        int glyph_h = line_h;
        if (glyph_h > line_h * 3 / 2) {
            seg_w = (int)(seg_w * (float)line_h / (float)glyph_h);
            glyph_h = line_h;
        }

        total_w += seg_w;
        if (glyph_h > max_h) max_h = glyph_h;
        free(seg_buf);
    }

    *out_w = (uint16_t)total_w;
    *out_h = (uint16_t)max_h;

    /* Store in measure cache */
    if (s_measure_cache_count < MEASURE_CACHE_MAX) {
        s_measure_cache[s_measure_cache_count].hash      = hash;
        s_measure_cache[s_measure_cache_count].font_size  = font_size;
        s_measure_cache[s_measure_cache_count].w          = (uint16_t)total_w;
        s_measure_cache[s_measure_cache_count].h          = (uint16_t)max_h;
        s_measure_cache_count++;
    }

    return ESP_OK;
}

/* ---- Public API: draw text ---- */

esp_err_t font_android_draw_text(int x, int y, const char *text, uint8_t font_size,
                                  uint16_t text_color, bool has_bg, uint16_t bg_color,
                                  int disp_w, int disp_h, uint16_t *pixels)
{
    if (!text || !text[0] || !pixels) return ESP_ERR_INVALID_ARG;

    int ptsize = font_size;
    if (font_stack_load(ptsize) == 0) return ESP_OK;

    /* Hold cache mutex for entire draw call — prevents LRU evictions
     * from freeing glyphs while we're still rendering them. */
    pthread_mutex_lock(&s_glyph_cache_mutex);

    int line_h = font_stack_line_height();
    const char *p = text;
    int cx = x;

    uint8_t tr, tg, tb;
    rgb565_to_rgb(text_color, &tr, &tg, &tb);

    while (*p) {
        uint32_t cp = utf8_decode(&p);
        android_font_t *font = font_for_codepoint(cp);

        /* Find font index for cache key */
        int font_idx = 0;
        for (int i = 0; i < s_font_stack_count; i++) {
            if (&s_font_stack[i] == font) { font_idx = i; break; }
        }

        glyph_bitmap_t *glyph = glyph_cache_lookup(cp, ptsize, font_idx,
                                                    FA_GLYPH_COLOR_SENT);
        bool new_glyph = false;
        glyph_bitmap_t rendered;
        glyph_bitmap_t *free_me = NULL;

        if (!glyph) {
            free_me = render_glyph(cp, ptsize, font);
            glyph = free_me;
            if (!glyph) {
                free_me = render_glyph_jni(cp, ptsize);
                glyph = free_me;
            }
            if (!glyph) continue;

            /* Scale down oversized glyphs to line height */
            if (glyph->h > line_h * 3 / 2) {
                float scale_f = (float)line_h / (float)glyph->h;
                int new_w = (int)(glyph->w * scale_f);
                if (new_w <= 0) new_w = 1;
                uint8_t *scaled = malloc((size_t)new_w * line_h * 4);
                if (scaled) {
                    for (int sy = 0; sy < line_h; sy++) {
                        int src_y = (int)(sy / scale_f + 0.5f);
                        if (src_y >= glyph->h) src_y = glyph->h - 1;
                        for (int sx = 0; sx < new_w; sx++) {
                            int src_x = (int)(sx / scale_f + 0.5f);
                            if (src_x >= glyph->w) src_x = glyph->w - 1;
                            uint8_t *spix = glyph->pixels + (src_y * glyph->w + src_x) * 4;
                            uint8_t *dpix = scaled + (sy * new_w + sx) * 4;
                            dpix[0] = spix[0]; dpix[1] = spix[1];
                            dpix[2] = spix[2]; dpix[3] = spix[3];
                        }
                    }
                    free_glyph_bitmap(free_me);
                    free_me = NULL;
                    rendered.w = new_w;
                    rendered.h = line_h;
                    rendered.pixels = scaled;
                    glyph = &rendered;
                    new_glyph = true;
                }
            }

            glyph_cache_insert(cp, ptsize, font_idx, FA_GLYPH_COLOR_SENT, glyph);
            glyph_cache_save(cp, ptsize, font_idx, FA_GLYPH_COLOR_SENT, glyph);
        }

        /* Copy glyph data locally so we can release cache lock */
        int gw = glyph->w, gh = glyph->h;
        uint8_t *gp_local = malloc((size_t)gw * gh * 4);
        if (gp_local) {
            memcpy(gp_local, glyph->pixels, (size_t)gw * gh * 4);
        } else {
            if (free_me) free(free_me);
            continue;
        }

        /* Cleanup heap-allocated struct (pixels are cache-owned or copied above) */
        if (free_me) free(free_me);

        /* Background fill */
        if (has_bg) {
            for (int gy = 0; gy < gh; gy++) {
                for (int gx = 0; gx < gw; gx++) {
                    int dx = cx + gx, dy = y + gy;
                    if (dx >= 0 && dx < disp_w && dy >= 0 && dy < disp_h)
                        pixels[dy * disp_w + dx] = bg_color;
                }
            }
        }

        /* Tinted alpha-blit from local copy */
        bool is_emoji = (cp > 0xFFFF && s_font_stack_count > 1 &&
                         font_idx == s_font_stack_count - 1);

        for (int gy = 0; gy < gh; gy++) {
            for (int gx = 0; gx < gw; gx++) {
                int dx = cx + gx, dy = y + gy;
                if (dx < 0 || dx >= disp_w || dy < 0 || dy >= disp_h) continue;

                uint8_t *src = gp_local + (gy * gw + gx) * 4;
                uint8_t ga = src[3];
                if (ga == 0) continue;

                uint16_t *dp = pixels + dy * disp_w + dx;

                if (ga == 255 && !is_emoji) {
                    *dp = text_color;
                    continue;
                }

                uint8_t dr, dg, db;
                rgb565_to_rgb(*dp, &dr, &dg, &db);

                int inv_a = 255 - ga;
                if (is_emoji) {
                    uint8_t nr = (uint8_t)(((int)src[0] * ga + (int)dr * inv_a) / 255);
                    uint8_t ng = (uint8_t)(((int)src[1] * ga + (int)dg * inv_a) / 255);
                    uint8_t nb = (uint8_t)(((int)src[2] * ga + (int)db * inv_a) / 255);
                    *dp = rgb_to_565(nr, ng, nb);
                } else {
                    uint8_t nr = (uint8_t)(((int)tr * ga + (int)dr * inv_a) / 255);
                    uint8_t ng = (uint8_t)(((int)tg * ga + (int)dg * inv_a) / 255);
                    uint8_t nb = (uint8_t)(((int)tb * ga + (int)db * inv_a) / 255);
                    *dp = rgb_to_565(nr, ng, nb);
                }
            }
        }

        free(gp_local);
        cx += gw;
    }

    pthread_mutex_unlock(&s_glyph_cache_mutex);
    return ESP_OK;
}

/* ---- Public API: aligned text ---- */

esp_err_t font_android_draw_text_aligned(int x, int y, int w, int h,
                                          const char *text, uint8_t font_size,
                                          uint16_t text_color, bool has_bg, uint16_t bg_color,
                                          display_hal_text_align_t align,
                                          display_hal_text_valign_t valign,
                                          int disp_w, int disp_h, uint16_t *pixels)
{
    if (!text || !pixels) return ESP_ERR_INVALID_ARG;

    uint16_t tw, th;
    if (font_android_measure_text(text, font_size, &tw, &th) != ESP_OK)
        return ESP_FAIL;

    int draw_x, draw_y;

    switch (align) {
        case DISPLAY_HAL_TEXT_ALIGN_CENTER: draw_x = x + (w - tw) / 2; break;
        case DISPLAY_HAL_TEXT_ALIGN_RIGHT:  draw_x = x + w - tw; break;
        default:                            draw_x = x; break;
    }

    switch (valign) {
        case DISPLAY_HAL_TEXT_VALIGN_MIDDLE: draw_y = y + (h - th) / 2; break;
        case DISPLAY_HAL_TEXT_VALIGN_BOTTOM: draw_y = y + h - th; break;
        default:                             draw_y = y; break;
    }

    return font_android_draw_text(draw_x, draw_y, text, font_size,
                                   text_color, has_bg, bg_color,
                                   disp_w, disp_h, pixels);
}

/* ---- Init / deinit ---- */

esp_err_t font_android_init(const char *data_dir)
{
    /* Init recursive cache mutex (held across entire draw calls) */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_glyph_cache_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    discover_fonts_android(data_dir);

    if (s_font_path_count == 0) {
        ESP_LOGE(TAG, "No fonts discovered. Using built-in 8x16 fallback.");
        return ESP_FAIL;
    }

    /* Set up glyph cache directory */
    if (data_dir) {
        snprintf(s_glyph_cache_dir, sizeof(s_glyph_cache_dir),
                 "%s/glyph_cache", data_dir);
        s_glyph_cache_dir_set = true;
        mkdir(s_glyph_cache_dir, 0755);
    }

    /* Pre-load fonts at default size 16 to enable cache pre-warm */
    font_stack_load(16);
    glyph_cache_load_all();

    ESP_LOGI(TAG, "Font init ok: %d fonts, %d cached glyphs",
             s_font_stack_count, s_glyph_cache_count);
    return ESP_OK;
}

void font_android_deinit(void)
{
    glyph_cache_clear();
    font_stack_close();

    for (int i = 0; i < s_font_path_count; i++) {
        if (s_font_paths[i]) free(s_font_paths[i]);
    }
    s_font_path_count = 0;
    ESP_LOGI(TAG, "Font deinit");
}
