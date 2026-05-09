/*
 * png.h — Minimal libpng stub for Android
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PNG_IMAGE_VERSION 1

/* Format constants needed by lua_module_display */
#define PNG_FORMAT_FLAG_COLOR      0x01
#define PNG_FORMAT_FLAG_ALPHA      0x02
#define PNG_FORMAT_FLAG_LINEAR     0x04
#define PNG_FORMAT_RGBA            (PNG_FORMAT_FLAG_COLOR | PNG_FORMAT_FLAG_ALPHA)

typedef int32_t png_int_32;

typedef struct {
    uint32_t version;
    int width;
    int height;
    int format;
    int flags;
    uint32_t colormap_entries;
    int warning_or_error;
} png_image;

typedef uint8_t *png_bytep;
typedef const uint8_t *png_const_bytep;

/* Stub — always fails */
static inline int png_image_begin_read_from_memory(png_image *image,
                                                    png_const_bytep memory,
                                                    size_t size)
{
    (void)memory; (void)size;
    image->warning_or_error = 1;
    return 0;
}

static inline int png_image_finish_read(png_image *image,
                                         png_const_bytep background,
                                         png_bytep *buffer,
                                         int row_stride,
                                         png_bytep *colormap)
{
    (void)background; (void)buffer; (void)row_stride; (void)colormap;
    image->warning_or_error = 1;
    return 0;
}

static inline void png_image_free(png_image *image)
{
    (void)image;
}

#ifdef __cplusplus
}
#endif
