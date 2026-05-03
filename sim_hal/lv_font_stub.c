/*
 * lv_font_stub.c — Minimal LVGL font function stubs.
 *
 * gfx and emote_expression use lv_font_t function pointers for font rendering,
 * but gfx has its own adapter that accesses font data directly.  These stubs
 * exist only to satisfy the linker.
 */
#include "lvgl.h"

const uint8_t *lv_font_get_bitmap_fmt_txt(const lv_font_t *font, uint32_t letter)
{
    (void)font; (void)letter;
    return NULL;
}

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                   uint32_t unicode_letter, uint32_t unicode_letter_next)
{
    (void)font; (void)dsc_out; (void)unicode_letter; (void)unicode_letter_next;
    return false;
}

const uint8_t *lv_font_get_glyph_bitmap(const lv_font_t *font_p, uint32_t letter)
{
    (void)font_p; (void)letter;
    return NULL;
}

bool lv_font_get_glyph_dsc(const lv_font_t *font_p, lv_font_glyph_dsc_t *dsc_out,
                           uint32_t letter, uint32_t letter_next)
{
    (void)font_p; (void)dsc_out; (void)letter; (void)letter_next;
    return false;
}

uint16_t lv_font_get_glyph_width(const lv_font_t *font, uint32_t letter, uint32_t letter_next)
{
    (void)font; (void)letter; (void)letter_next;
    return 0;
}
