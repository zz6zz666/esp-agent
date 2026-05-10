/*
 * font_android.h — Android font rendering system
 *
 * TrueType rendering via FreeType 2 (same engine as SDL2_ttf on desktop)
 * using Android system fonts + bundled fallbacks.
 *
 * Architecture mirrors display_sdl2.c's font system:
 *   - Multi-font stack with fallback chain (up to 6 fonts)
 *   - Per-codepoint font selection
 *   - 512-entry LRU glyph cache (memory + persistent disk cache)
 *   - Tinted alpha-blit text rendering
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "display_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FA_GLYPH_CACHE_MAX   512
#define FA_GLYPH_CACHE_MAGIC 0xCAFF
#define FA_MAX_FONT_STACK     6
#define FA_GLYPH_COLOR_SENT   0xFFFF  /* cache key: all glyphs stored white */

/* Init the font system. data_dir is the cr. claw data directory
 * (used for glyph cache persistence and bundled font lookup). */
esp_err_t font_android_init(const char *data_dir);

/* Deinit: flush persistent cache, free font data. */
void font_android_deinit(void);

/* Measure text dimensions (pixels). Same signature as display_hal_measure_text. */
esp_err_t font_android_measure_text(const char *text, uint8_t font_size,
                                    uint16_t *out_w, uint16_t *out_h);

/* Draw text at (x,y). Same signature as display_hal_draw_text. */
esp_err_t font_android_draw_text(int x, int y, const char *text, uint8_t font_size,
                                  uint16_t text_color, bool has_bg, uint16_t bg_color,
                                  int disp_w, int disp_h, uint16_t *pixels);

/* Draw aligned text within bounds. */
esp_err_t font_android_draw_text_aligned(int x, int y, int w, int h,
                                          const char *text, uint8_t font_size,
                                          uint16_t text_color, bool has_bg, uint16_t bg_color,
                                          display_hal_text_align_t align,
                                          display_hal_text_valign_t valign,
                                          int disp_w, int disp_h, uint16_t *pixels);

#ifdef __cplusplus
}
#endif
