/* Stub for qrcodegen.h */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t data[256]; int size; } QRCode;
static inline bool qrcodegen_encodeText(const char *text, uint8_t *buf, int ver, int ecc,
                                          uint8_t *out, int out_sz, int *out_size) {
    (void)text; (void)buf; (void)ver; (void)ecc; (void)out; (void)out_sz; (void)out_size;
    return false;
}
