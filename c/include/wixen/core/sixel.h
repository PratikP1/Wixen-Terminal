/* sixel.h — Sixel image protocol decoder */
#ifndef WIXEN_CORE_SIXEL_H
#define WIXEN_CORE_SIXEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decoded Sixel image — RGBA pixel buffer */
typedef struct {
    uint8_t *pixels;    /* RGBA data (4 bytes per pixel) */
    uint32_t width;
    uint32_t height;
    uint32_t stride;    /* Bytes per row */
} WixenSixelImage;

/* Decode a Sixel data stream into an RGBA pixel buffer.
 * data/len is the payload between DCS q ... ST.
 * Returns false on invalid or empty input. */
bool wixen_sixel_decode(const char *data, size_t len, WixenSixelImage *out);

/* Free decoded image data */
void wixen_sixel_free(WixenSixelImage *img);

#endif /* WIXEN_CORE_SIXEL_H */
