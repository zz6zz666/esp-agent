/*
 * font_android.c — Android TrueType font rendering via FreeType
 *
 * TrueType rendering with FreeType 2, the same engine used by SDL2_ttf
 * on the desktop.  This gives us:
 *   - Proper hinting (pixel grid alignment / FT_Set_Pixel_Sizes)
 *   - Correct baseline positioning (bitmap_top / bitmap_left)
 *   - Correct per-glyph advance
 *   - Rendering identical to the desktop build
 *
 * FreeType and libpng static libraries are cross-compiled for Android via
 * third_party/build_freetype_android.sh and linked in CMakeLists.txt.
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
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "display_hal.h"
#include "esp_err.h"
#include "esp_log.h"
#include "font_android.h"

static const char *TAG = "font_android";

/* ---- Font descriptor ---- */
typedef struct {
    FT_Face         face;
    uint8_t        *font_data;
    size_t          font_size;
    char           *path;
    const char     *label;
} android_font_t;

/* ---- Font stack ---- */
static android_font_t s_font_stack[FA_MAX_FONT_STACK];
static int             s_font_stack_count = 0;

/* ---- Font paths ---- */
static char        *s_font_paths[FA_MAX_FONT_STACK];
static const char  *s_font_labels[FA_MAX_FONT_STACK];
static int          s_font_path_count = 0;

/* ---- Glyph bitmap (RGBA32) ---- */
typedef struct {
    int      w, h;
    uint8_t *pixels;
    int      offset_left;  /* bitmap_left  at target ptsize */
    int      offset_top;   /* bitmap_top   at target ptsize */
    int      advance;      /* advance.x>>6 at target ptsize */
    bool     is_color;     /* true if pixel data is native colour */
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
static pthread_mutex_t     s_glyph_cache_mutex;
static pthread_mutex_t     s_glyph_disk_mutex  = PTHREAD_MUTEX_INITIALIZER;

static char s_glyph_cache_dir[512];
static bool s_glyph_cache_dir_set = false;

static FT_Library s_ft_library = NULL;

/* ---- Forward declarations ---- */
static void font_stack_close(void);
static int  font_stack_load(void);
static android_font_t *font_for_codepoint(uint32_t cp);
static int  font_stack_line_height(void);
static uint32_t utf8_decode(const char **p);
static glyph_bitmap_t *glyph_cache_lookup(uint32_t cp, int ptsize, int font_idx, uint16_t color565);
static void glyph_cache_add_entry(uint32_t cp, int ptsize, int font_idx, uint16_t color565, glyph_bitmap_t *bitmap);
static void glyph_cache_save(uint32_t cp, int ptsize, int font_idx, uint16_t color565, glyph_bitmap_t *bitmap);
static void glyph_cache_load_all(void);
static void glyph_cache_clear(void);
static glyph_bitmap_t *render_glyph(uint32_t cp, int ptsize, android_font_t *font);
static void free_glyph_bitmap(glyph_bitmap_t *b);

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

    /* Bundled fonts (from APK assets) — checked first, take priority */
    if (data_dir) {
        char font_dir[512];
        snprintf(font_dir, sizeof(font_dir), "%s/fonts", data_dir);
        try_font_path(font_dir, "DejaVuSans.ttf",      "DejaVu Sans");
        try_font_path(font_dir, "wqy-zenhei.ttc",      "WenQuanYi Zen Hei");
        try_font_path(font_dir, "NotoColorEmoji.ttf",  "Noto Color Emoji");
    }

    /* System fonts (fallback) */
    try_font_path("/system/fonts", "Roboto-Regular.ttf",    "Roboto");
    try_font_path("/system/fonts", "NotoColorEmoji.ttf",    "Noto Color Emoji");
    try_font_path("/system/fonts", "NotoSansCJK-Regular.ttc","Noto Sans CJK");
    try_font_path("/system/fonts", "DroidSansFallback.ttf", "Droid Sans Fallback");
    try_font_path("/system/fonts", "DroidSans.ttf",         "Droid Sans");
    try_font_path("/system/fonts", "NotoSansSC-Regular.otf", "Noto Sans SC");
    try_font_path("/system/fonts", "NotoSansSC-Regular.ttf", "Noto Sans SC");

    if (s_font_path_count == 0) {
        try_font_path("/system/fonts", "DroidSans.ttf",     "Droid Sans");
    }

    ESP_LOGI(TAG, "Font discovery: %d fonts found", s_font_path_count);
}

/* ---- Font stack lifecycle ---- */

static void font_stack_close(void)
{
    for (int i = 0; i < s_font_stack_count; i++) {
        if (s_font_stack[i].face) {
            FT_Done_Face(s_font_stack[i].face);
            s_font_stack[i].face = NULL;
        }
        if (s_font_stack[i].font_data) {
            free(s_font_stack[i].font_data);
            s_font_stack[i].font_data = NULL;
        }
        memset(&s_font_stack[i], 0, sizeof(android_font_t));
    }
    s_font_stack_count = 0;
}

static int font_stack_load(void)
{
    if (s_font_stack_count > 0)
        return s_font_stack_count;

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

        FT_Error err = FT_New_Memory_Face(s_ft_library, data, (FT_Long)sz, 0, &f->face);
        if (err) {
            free(data);
            ESP_LOGW(TAG, "FT_New_Memory_Face failed (err=%d) for %s", err, s_font_paths[i]);
            continue;
        }

        f->font_data = data;
        f->font_size = sz;
        f->path      = s_font_paths[i];
        f->label     = s_font_labels[i];

        ESP_LOGI(TAG, "Font loaded: %s (%s) faces=%ld fixed_sizes=%d",
                 f->label, f->path, (long)f->face->num_faces,
                 f->face->num_fixed_sizes);
        for (int j = 0; j < f->face->num_fixed_sizes; j++) {
            ESP_LOGI(TAG, "  strike[%d]: %dx%d",
                     j, f->face->available_sizes[j].width,
                     f->face->available_sizes[j].height);
        }
        s_font_stack_count++;
    }

    return s_font_stack_count;
}

static android_font_t *font_for_codepoint(uint32_t cp)
{
    if (s_font_stack_count == 0) return NULL;

    /* For supplementary-plane characters (real emoji: U+1Fxxx, etc.),
       prefer a colour-emoji font (CBDT bitmap strikes) so they render
       in colour instead of borrowing a monochrome glyph from an earlier
       font. */
    if (cp > 0xFFFF) {
        for (int i = 0; i < s_font_stack_count; i++) {
            if ((s_font_stack[i].face->face_flags & FT_FACE_FLAG_FIXED_SIZES) &&
                s_font_stack[i].face->num_fixed_sizes > 0 &&
                FT_Get_Char_Index(s_font_stack[i].face, (FT_ULong)cp) != 0) {
                return &s_font_stack[i];
            }
        }
    }

    for (int i = 0; i < s_font_stack_count; i++) {
        if (FT_Get_Char_Index(s_font_stack[i].face, (FT_ULong)cp) != 0)
            return &s_font_stack[i];
    }

    return &s_font_stack[0];
}

static int font_stack_line_height(void)
{
    if (s_font_stack_count == 0) return 16;
    android_font_t *f = &s_font_stack[0];
    FT_Face face = f->face;
    FT_UShort upem = face->units_per_EM;
    int height = (int)((int64_t)face->height * face->size->metrics.y_ppem / upem);
    return height > 0 ? height : 16;
}

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

static glyph_bitmap_t *render_glyph(uint32_t cp, int ptsize, android_font_t *font)
{
    if (!font || !font->face) return NULL;

    FT_Face face = font->face;
    FT_Error err;
    FT_Int32 load_flags = FT_LOAD_COLOR | FT_LOAD_RENDER;
    bool need_downscale = false;
    float scale = 1.0f;

    /* Bitmap-only font (CBDT emoji) — select exact bitmap strike, then downscale. */
    if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) && face->num_fixed_sizes > 0) {
        FT_Select_Size(face, 0);
        if (face->available_sizes[0].height > (FT_UShort)ptsize) {
            scale = (float)ptsize / (float)face->available_sizes[0].height;
            need_downscale = true;
        }
        load_flags |= FT_LOAD_NO_SCALE;
    } else {
        err = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)ptsize);
        if (err) {
            ESP_LOGW(TAG, "FT_Set_Pixel_Sizes(%d) failed err=%d font=%s",
                     ptsize, err, font->label);
            return NULL;
        }
    }

    FT_UInt glyph_index = FT_Get_Char_Index(face, (FT_ULong)cp);
    if (glyph_index == 0) return NULL;

    err = FT_Load_Glyph(face, glyph_index, load_flags);
    if (err) {
        err = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_RENDER);
    }
    if (err) {
        ESP_LOGW(TAG, "FT_Load_Glyph(U+%04X) failed err=%d", cp, err);
        return NULL;
    }

    FT_Bitmap *bmp = &face->glyph->bitmap;
    if (bmp->width == 0 || bmp->rows == 0) return NULL;

    bool is_color = (bmp->pixel_mode == FT_PIXEL_MODE_BGRA);

    FT_Glyph_Metrics *m = &face->glyph->metrics;
    int raw_advance = (int)(face->glyph->advance.x >> 6);
    int raw_left    = face->glyph->bitmap_left;
    int raw_top     = face->glyph->bitmap_top;

    int dst_w, dst_h;
    int src_w = (int)bmp->width;
    int src_h = (int)bmp->rows;

    if (need_downscale) {
        dst_w = (int)((float)src_w * scale + 0.5f);
        dst_h = (int)((float)src_h * scale + 0.5f);
        if (dst_w < 1) dst_w = 1;
        if (dst_h < 1) dst_h = 1;
    } else {
        dst_w = src_w;
        dst_h = src_h;
    }

    glyph_bitmap_t *gb = calloc(1, sizeof(*gb));
    if (!gb) return NULL;

    gb->w      = dst_w;
    gb->h      = dst_h;
    gb->pixels = malloc((size_t)dst_w * dst_h * 4);
    if (!gb->pixels) { free(gb); return NULL; }

    if (need_downscale) {
        float inv_scale = 1.0f / scale;
        for (int dy = 0; dy < dst_h; dy++) {
            float sy0 = (float)dy * inv_scale;
            float sy1 = sy0 + inv_scale;
            if (sy1 > src_h) sy1 = (float)src_h;
            for (int dx = 0; dx < dst_w; dx++) {
                float sx0 = (float)dx * inv_scale;
                float sx1 = sx0 + inv_scale;
                if (sx1 > src_w) sx1 = (float)src_w;

                /* Box filter: accumulate all source samples in the footprint */
                float r = 0, g = 0, b = 0, a = 0, cnt = 0;
                for (int sy = (int)sy0; sy < (int)sy1; sy++) {
                    uint8_t *srow = bmp->buffer + (size_t)sy * (size_t)bmp->pitch;
                    for (int sx = (int)sx0; sx < (int)sx1; sx++) {
                        if (is_color) {
                            uint8_t *p = srow + (size_t)sx * 4;
                            float sa = (float)p[3] / 255.0f;
                            b += (float)p[0] * sa; g += (float)p[1] * sa;
                            r += (float)p[2] * sa; a += sa;
                        } else {
                            a += (float)srow[sx] / 255.0f;
                        }
                        cnt += 1.0f;
                    }
                }
                uint8_t *dst = gb->pixels + (size_t)(dy * dst_w + dx) * 4;
                if (cnt > 0 && a > 0) {
                    a /= cnt;
                    if (is_color) {
                        dst[0] = (uint8_t)(r / (a * cnt));
                        dst[1] = (uint8_t)(g / (a * cnt));
                        dst[2] = (uint8_t)(b / (a * cnt));
                        dst[3] = (uint8_t)(a * 255.0f);
                    } else {
                        dst[0] = 255; dst[1] = 255; dst[2] = 255;
                        dst[3] = (uint8_t)(a * 255.0f);
                    }
                } else {
                    dst[0] = dst[1] = dst[2] = dst[3] = 0;
                }
            }
        }

        gb->offset_left = (int)((float)raw_left * scale + 0.5f);
        gb->offset_top  = (int)((float)raw_top  * scale + 0.5f);
        gb->advance     = (int)((float)raw_advance * scale + 0.5f);
        gb->is_color    = is_color;
    } else {
        if (is_color) {
            for (int y = 0; y < src_h; y++) {
                for (int x = 0; x < src_w; x++) {
                    uint8_t *src = bmp->buffer + (size_t)y * (size_t)bmp->pitch + (size_t)x * 4;
                    uint8_t *dst = gb->pixels + (size_t)(y * src_w + x) * 4;
                    dst[0] = src[2]; dst[1] = src[1];
                    dst[2] = src[0]; dst[3] = src[3];
                }
            }
        } else {
            for (int y = 0; y < src_h; y++) {
                for (int x = 0; x < src_w; x++) {
                    uint8_t *src = bmp->buffer + y * bmp->pitch + x;
                    uint8_t *dst = gb->pixels + (size_t)(y * src_w + x) * 4;
                    dst[0] = 255; dst[1] = 255; dst[2] = 255; dst[3] = *src;
                }
            }
        }

        gb->offset_left = raw_left;
        gb->offset_top  = raw_top;
        gb->advance     = raw_advance;
        gb->is_color    = is_color;
    }

    return gb;
}

static void free_glyph_bitmap(glyph_bitmap_t *b)
{
    if (!b) return;
    if (b->pixels) free(b->pixels);
    b->pixels = NULL;
}

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
    int32_t  ol32 = (int32_t)bitmap->offset_left;
    int32_t  ot32 = (int32_t)bitmap->offset_top;
    int32_t  av32 = (int32_t)bitmap->advance;
    uint8_t  ic8  = (uint8_t)(bitmap->is_color ? 1 : 0);
    uint32_t cp32 = cp;
    uint32_t pt32 = (uint32_t)ptsize;
    uint32_t fi32 = (uint32_t)font_idx;
    fwrite(&cp32, 4, 1, f);
    fwrite(&pt32, 4, 1, f);
    fwrite(&fi32, 4, 1, f);
    fwrite(&color565, 2, 1, f);
    fwrite(&ol32, 4, 1, f);
    fwrite(&ot32, 4, 1, f);
    fwrite(&av32, 4, 1, f);
    fwrite(&ic8, 1, 1, f);
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
        int32_t  ol32, ot32, av32;
        uint8_t  ic8;
        uint16_t color565;
        if (fread(&cp32, 4, 1, f) != 1 ||
            fread(&pt32, 4, 1, f) != 1 ||
            fread(&fi32, 4, 1, f) != 1 ||
            fread(&color565, 2, 1, f) != 1 ||
            fread(&ol32, 4, 1, f) != 1 ||
            fread(&ot32, 4, 1, f) != 1 ||
            fread(&av32, 4, 1, f) != 1 ||
            fread(&ic8, 1, 1, f) != 1 ||
            fread(&w32, 4, 1, f) != 1 ||
            fread(&h32, 4, 1, f) != 1) break;

        if (w32 == 0 || h32 == 0 || w32 > 4096 || h32 > 4096) {
            fseek(f, (long)w32 * h32 * 4, SEEK_CUR); continue;
        }
        if ((int)fi32 >= s_font_stack_count) {
            fseek(f, (long)w32 * h32 * 4, SEEK_CUR); continue;
        }

        glyph_bitmap_t bm = {0};
        bm.w           = (int)w32;
        bm.h           = (int)h32;
        bm.offset_left = (int)ol32;
        bm.offset_top  = (int)ot32;
        bm.advance     = (int)av32;
        bm.is_color    = (ic8 != 0);
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

/* Thread-safe insert (takes lock).  Mirrors desktop glyph_cache_insert(). */
static void glyph_cache_insert(uint32_t cp, int ptsize, int font_idx,
                               uint16_t color565, glyph_bitmap_t *bitmap)
{
    if (!bitmap) return;
    pthread_mutex_lock(&s_glyph_cache_mutex);
    glyph_cache_add_entry(cp, ptsize, font_idx, color565, bitmap);
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

/* ---- Public API: measure text ---- */

esp_err_t font_android_measure_text(const char *text, uint8_t font_size,
                                     uint16_t *out_w, uint16_t *out_h)
{
    if (!text || !out_w || !out_h) return ESP_ERR_INVALID_ARG;

    if (font_stack_load() == 0) {
        *out_w = (uint16_t)(strlen(text) * 8 * font_size);
        *out_h = (uint16_t)(16 * font_size);
        return ESP_OK;
    }

    int ptsize = font_size;
    int total_w = 0;
    const char *p = text;

    while (*p) {
        uint32_t cp = utf8_decode(&p);
        android_font_t *font = font_for_codepoint(cp);
        if (!font) continue;

        FT_Face face = font->face;

        /* Bitmap-only font: use native-size metrics, downscale to ptsize */
        if ((face->face_flags & FT_FACE_FLAG_FIXED_SIZES) && face->num_fixed_sizes > 0) {
            FT_Select_Size(face, 0);
            float scale = (float)ptsize / (float)face->available_sizes[0].height;
            FT_UInt gi = FT_Get_Char_Index(face, (FT_ULong)cp);
            if (gi == 0) continue;
            if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT | FT_LOAD_NO_SCALE) == 0) {
                int adv = (int)((float)(face->glyph->advance.x >> 6) * scale + 0.5f);
                if (adv > 0) total_w += adv;
            }
        } else {
            FT_Set_Pixel_Sizes(face, 0, (FT_UInt)ptsize);
            FT_UInt gi = FT_Get_Char_Index(face, (FT_ULong)cp);
            if (gi == 0) continue;
            if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT) == 0) {
                int adv = (int)(face->glyph->advance.x >> 6);
                if (adv > 0) total_w += adv;
            }
        }
    }

    /* Set size on first font so line_height reads correctly */
    if (s_font_stack_count > 0) {
        FT_Set_Pixel_Sizes(s_font_stack[0].face, 0, (FT_UInt)ptsize);
    }
    int line_h = font_stack_line_height();

    *out_w = (uint16_t)total_w;
    *out_h = (uint16_t)line_h;
    return ESP_OK;
}

/* ---- Public API: draw text ---- */

esp_err_t font_android_draw_text(int x, int y, const char *text, uint8_t font_size,
                                   uint16_t text_color, bool has_bg, uint16_t bg_color,
                                   int disp_w, int disp_h, uint16_t *pixels)
{
    if (!text || !text[0] || !pixels) return ESP_ERR_INVALID_ARG;

    int ptsize = font_size;
    if (font_stack_load() == 0) return ESP_OK;

    android_font_t *font0 = &s_font_stack[0];
    FT_Set_Pixel_Sizes(font0->face, 0, (FT_UInt)ptsize);

    int ascent = (int)(font0->face->size->metrics.ascender >> 6);
    int baseline = y + ascent;
    int cx = x;

    uint8_t tr, tg, tb;
    rgb565_to_rgb(text_color, &tr, &tg, &tb);

    const char *p = text;
    while (*p) {
        uint32_t cp = utf8_decode(&p);
        android_font_t *font = font_for_codepoint(cp);
        if (!font) continue;

        FT_Face face = font->face;

        int font_idx = 0;
        for (int i = 0; i < s_font_stack_count; i++) {
            if (&s_font_stack[i] == font) { font_idx = i; break; }
        }

        /* Lookup in cache (lock taken briefly by glyph_cache_lookup). */
        glyph_bitmap_t *glyph = glyph_cache_lookup(cp, ptsize, font_idx,
                                                     FA_GLYPH_COLOR_SENT);
        glyph_bitmap_t *rendered = NULL;

        if (!glyph) {
            rendered = render_glyph(cp, ptsize, font);
            glyph = rendered;
            if (!glyph) {
                if (FT_Load_Glyph(face, FT_Get_Char_Index(face, (FT_ULong)cp),
                                  FT_LOAD_DEFAULT) == 0) {
                    cx += (int)(face->glyph->advance.x >> 6);
                }
                continue;
            }
            /* Insert into cache (lock taken briefly by glyph_cache_insert).
               NOTE: no disk I/O on the hot path — desktop never saves here. */
            glyph_cache_insert(cp, ptsize, font_idx,
                               FA_GLYPH_COLOR_SENT, glyph);
        }

        int gw = glyph->w, gh = glyph->h;
        if (gw == 0 || gh == 0) {
            if (rendered) free(rendered);
            cx += glyph->advance;
            continue;
        }

        /* Copy pixels out of the cache so we can release the lock before
           the expensive tinted-blit loop. */
        uint8_t *gp_local = malloc((size_t)gw * gh * 4);
        if (!gp_local) {
            if (rendered) free(rendered);
            cx += glyph->advance;
            continue;
        }
        memcpy(gp_local, glyph->pixels, (size_t)gw * gh * 4);

        int offset_left = glyph->offset_left;
        int offset_top  = glyph->offset_top;
        int advance     = glyph->advance;
        bool is_color   = glyph->is_color;

        if (rendered) free(rendered);

        int draw_x = cx + offset_left;
        int draw_y = baseline - offset_top;

        if (has_bg) {
            int bg_x0 = draw_x > 0 ? draw_x : 0;
            int bg_y0 = draw_y > 0 ? draw_y : 0;
            int bg_x1 = draw_x + gw < disp_w ? draw_x + gw : disp_w;
            int bg_y1 = draw_y + gh < disp_h ? draw_y + gh : disp_h;
            if (bg_x1 > bg_x0 && bg_y1 > bg_y0) {
                for (int gy = bg_y0; gy < bg_y1; gy++) {
                    for (int gx = bg_x0; gx < bg_x1; gx++) {
                        pixels[gy * disp_w + gx] = bg_color;
                    }
                }
            }
        }

        for (int gy = 0; gy < gh; gy++) {
            for (int gx = 0; gx < gw; gx++) {
                int dx = draw_x + gx, dy = draw_y + gy;
                if (dx < 0 || dx >= disp_w || dy < 0 || dy >= disp_h) continue;

                uint8_t *src = gp_local + (gy * gw + gx) * 4;
                uint8_t ga = src[3];
                if (ga == 0) continue;

                uint16_t *dp = pixels + dy * disp_w + dx;

                if (ga == 255 && !is_color) {
                    *dp = text_color;
                    continue;
                }

                uint8_t dr, dg, db;
                rgb565_to_rgb(*dp, &dr, &dg, &db);

                int inv_a = 255 - ga;
                if (is_color) {
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
        cx += advance;
    }

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
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_glyph_cache_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    FT_Error err = FT_Init_FreeType(&s_ft_library);
    if (err) {
        ESP_LOGE(TAG, "FT_Init_FreeType failed (err=%d)", err);
        return ESP_FAIL;
    }

    discover_fonts_android(data_dir);

    if (s_font_path_count == 0) {
        ESP_LOGE(TAG, "No fonts discovered.");
        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;
        return ESP_FAIL;
    }

    if (data_dir) {
        snprintf(s_glyph_cache_dir, sizeof(s_glyph_cache_dir),
                 "%s/glyph_cache", data_dir);
        s_glyph_cache_dir_set = true;
        mkdir(s_glyph_cache_dir, 0755);
    }

    font_stack_load();
    glyph_cache_load_all();

    ESP_LOGI(TAG, "Font init ok: %d fonts, %d cached glyphs",
             s_font_stack_count, s_glyph_cache_count);
    return ESP_OK;
}

void font_android_deinit(void)
{
    glyph_cache_clear();
    font_stack_close();

    if (s_ft_library) {
        FT_Done_FreeType(s_ft_library);
        s_ft_library = NULL;
    }

    for (int i = 0; i < s_font_path_count; i++) {
        if (s_font_paths[i]) free(s_font_paths[i]);
    }
    s_font_path_count = 0;
    ESP_LOGI(TAG, "Font deinit");
}
