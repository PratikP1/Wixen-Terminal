/* image.h — Terminal image protocols (Sixel, iTerm2, Kitty) */
#ifndef WIXEN_CORE_IMAGE_H
#define WIXEN_CORE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t id;
    uint32_t width;           /* Pixels */
    uint32_t height;
    uint8_t *pixels;          /* RGBA row-major */
    size_t col, row;          /* Grid position */
    size_t cell_cols, cell_rows; /* Grid span */
} WixenTerminalImage;

typedef struct {
    WixenTerminalImage *images;
    size_t count;
    size_t cap;
    uint64_t next_id;
} WixenImageStore;

void wixen_images_init(WixenImageStore *store);
void wixen_images_free(WixenImageStore *store);
uint64_t wixen_images_add(WixenImageStore *store, uint32_t width, uint32_t height,
                           uint8_t *pixels, size_t col, size_t row,
                           size_t cell_cols, size_t cell_rows);
void wixen_images_clear(WixenImageStore *store);
size_t wixen_images_count(const WixenImageStore *store);
const WixenTerminalImage *wixen_images_get(const WixenImageStore *store, uint64_t id);
bool wixen_images_remove(WixenImageStore *store, uint64_t id);

/* Sixel decoder: decode DCS data into RGBA pixels.
 * Returns true on success. Caller must free *out_pixels. */
bool wixen_decode_sixel(const uint8_t *data, size_t len,
                         uint32_t *out_width, uint32_t *out_height,
                         uint8_t **out_pixels);

#endif /* WIXEN_CORE_IMAGE_H */
