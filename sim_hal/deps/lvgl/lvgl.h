/*
 * Minimal LVGL header stub — provides only font format types.
 * Needed by esp_emote_gfx and esp_emote_expression for lv_font_t etc.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- config ---- */
#define LV_FONT_FMT_TXT_LARGE  0
#define LV_USE_USER_DATA       1
#define LV_USE_LARGE_COORD     0
#define LV_ATTRIBUTE_LARGE_CONST const

#define LVGL_VERSION_MAJOR 8
#define LVGL_VERSION_MINOR 3
#define LVGL_VERSION_PATCH 0
#define LV_VERSION_CHECK(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define LV_FONT_MONTSERRAT_8   0
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  0
#define LV_FONT_MONTSERRAT_14  0
#define LV_FONT_MONTSERRAT_16  0
#define LV_FONT_MONTSERRAT_18  0
#define LV_FONT_MONTSERRAT_20  0
#define LV_FONT_MONTSERRAT_22  0
#define LV_FONT_MONTSERRAT_24  0
#define LV_FONT_MONTSERRAT_26  0
#define LV_FONT_MONTSERRAT_28  0
#define LV_FONT_MONTSERRAT_30  0
#define LV_FONT_MONTSERRAT_32  0
#define LV_FONT_MONTSERRAT_34  0
#define LV_FONT_MONTSERRAT_36  0
#define LV_FONT_MONTSERRAT_38  0
#define LV_FONT_MONTSERRAT_40  0
#define LV_FONT_MONTSERRAT_42  0
#define LV_FONT_MONTSERRAT_44  0
#define LV_FONT_MONTSERRAT_46  0
#define LV_FONT_MONTSERRAT_48  0
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0
#ifndef LV_FONT_DEFAULT
#define LV_FONT_DEFAULT NULL
#endif
#ifndef LV_FONT_CUSTOM_DECLARE
#define LV_FONT_CUSTOM_DECLARE
#endif

/* ---- lv_area.h ---- */
typedef int16_t lv_coord_t;

typedef struct { lv_coord_t x; lv_coord_t y; } lv_point_t;
typedef struct { lv_coord_t x1, y1, x2, y2; } lv_area_t;

/* ---- lv_symbol_def.h ---- */
#define LV_SYMBOL_BULLET  "\xEF\x80\xA7"

/* ---- lv_font.h ---- */
enum { LV_FONT_SUBPX_NONE, LV_FONT_SUBPX_HOR, LV_FONT_SUBPX_VER, LV_FONT_SUBPX_BOTH };
typedef uint8_t lv_font_subpx_t;

struct _lv_font_t;
typedef struct {
    const struct _lv_font_t *resolved_font;
    uint16_t adv_w, box_w, box_h;
    int16_t ofs_x, ofs_y;
    uint8_t bpp : 4;
    uint8_t is_placeholder : 1;
} lv_font_glyph_dsc_t;

typedef struct _lv_font_t {
    bool (*get_glyph_dsc)(const struct _lv_font_t *, lv_font_glyph_dsc_t *,
                          uint32_t letter, uint32_t letter_next);
    const uint8_t *(*get_glyph_bitmap)(const struct _lv_font_t *, uint32_t);
    lv_coord_t line_height;
    lv_coord_t base_line;
    uint8_t subpx : 2;
    int8_t underline_position;
    int8_t underline_thickness;
    const void *dsc;
    const struct _lv_font_t *fallback;
    void *user_data;
} lv_font_t;

/* ---- lv_font_fmt_txt.h ---- */
typedef struct {
    uint32_t bitmap_index : 20;
    uint32_t adv_w : 12;
    uint8_t box_w, box_h;
    int8_t ofs_x, ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
};
typedef uint8_t lv_font_fmt_txt_cmap_type_t;

typedef struct {
    uint32_t range_start;
    uint16_t range_length, glyph_id_start;
    const uint16_t *unicode_list;
    const void *glyph_id_ofs_list;
    uint16_t list_length;
    lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const void *glyph_ids;
    const int8_t *values;
    uint32_t pair_cnt : 30;
    uint32_t glyph_ids_size : 2;
} lv_font_fmt_txt_kern_pair_t;

typedef struct {
    const int8_t *class_pair_values;
    const uint8_t *left_class_mapping, *right_class_mapping;
    uint8_t left_class_cnt, right_class_cnt;
} lv_font_fmt_txt_kern_classes_t;

typedef enum {
    LV_FONT_FMT_TXT_PLAIN = 0,
    LV_FONT_FMT_TXT_COMPRESSED = 1,
    LV_FONT_FMT_TXT_COMPRESSED_NO_PREFILTER = 1,
} lv_font_fmt_txt_bitmap_format_t;

typedef struct {
    uint32_t last_letter, last_glyph_id;
} lv_font_fmt_txt_glyph_cache_t;

typedef struct {
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num : 9;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 2;
    lv_font_fmt_txt_glyph_cache_t *cache;
} lv_font_fmt_txt_dsc_t;

/* ---- function declarations (stub implementations) ---- */
const uint8_t *lv_font_get_bitmap_fmt_txt(const lv_font_t *font, uint32_t letter);
bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                   uint32_t unicode_letter, uint32_t unicode_letter_next);
const uint8_t *lv_font_get_glyph_bitmap(const lv_font_t *font_p, uint32_t letter);
bool lv_font_get_glyph_dsc(const lv_font_t *font_p, lv_font_glyph_dsc_t *dsc_out,
                           uint32_t letter, uint32_t letter_next);
uint16_t lv_font_get_glyph_width(const lv_font_t *font, uint32_t letter, uint32_t letter_next);

static inline lv_coord_t lv_font_get_line_height(const lv_font_t *font_p) {
    return font_p ? font_p->line_height : 0;
}
static inline const lv_font_t *lv_font_default(void) {
    return LV_FONT_DEFAULT;
}

#define LV_FONT_DECLARE(font_name) extern const lv_font_t font_name;
LV_FONT_CUSTOM_DECLARE

#ifdef __cplusplus
}
#endif
