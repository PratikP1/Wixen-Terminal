/* image.c — Terminal image store and Sixel decoder */
#include "wixen/core/image.h"
#include <stdlib.h>
#include <string.h>

void wixen_images_init(WixenImageStore *store) {
    memset(store, 0, sizeof(*store));
    store->next_id = 1;
}

void wixen_images_free(WixenImageStore *store) {
    for (size_t i = 0; i < store->count; i++) {
        free(store->images[i].pixels);
    }
    free(store->images);
    memset(store, 0, sizeof(*store));
}

uint64_t wixen_images_add(WixenImageStore *store, uint32_t width, uint32_t height,
                           uint8_t *pixels, size_t col, size_t row,
                           size_t cell_cols, size_t cell_rows) {
    if (store->count >= store->cap) {
        size_t new_cap = store->cap ? store->cap * 2 : 8;
        WixenTerminalImage *new_arr = realloc(store->images,
                                               new_cap * sizeof(WixenTerminalImage));
        if (!new_arr) return 0;
        store->images = new_arr;
        store->cap = new_cap;
    }

    uint64_t id = store->next_id++;
    WixenTerminalImage *img = &store->images[store->count++];
    img->id = id;
    img->width = width;
    img->height = height;
    img->pixels = pixels; /* Takes ownership */
    img->col = col;
    img->row = row;
    img->cell_cols = cell_cols;
    img->cell_rows = cell_rows;
    return id;
}

void wixen_images_clear(WixenImageStore *store) {
    for (size_t i = 0; i < store->count; i++) {
        free(store->images[i].pixels);
    }
    store->count = 0;
}

size_t wixen_images_count(const WixenImageStore *store) {
    return store->count;
}

/* --- Sixel decoder ---
 * Sixel is a DEC graphics protocol where each character encodes a 6-pixel-high column.
 * Characters 0x3F-0x7E represent 6 vertical pixels (subtract 0x3F to get bitmask).
 * #N selects color register N.
 * #N;2;R;G;B defines a color register.
 * $ = carriage return (back to left of current row)
 * - = newline (advance down 6 pixels)
 */

#define MAX_SIXEL_COLORS 256
#define SIXEL_MAX_WIDTH  4096
#define SIXEL_MAX_HEIGHT 4096

bool wixen_decode_sixel(const uint8_t *data, size_t len,
                         uint32_t *out_width, uint32_t *out_height,
                         uint8_t **out_pixels) {
    if (!data || len == 0) return false;

    /* Color palette */
    uint8_t palette[MAX_SIXEL_COLORS][3];
    memset(palette, 0, sizeof(palette));
    /* Default: first 16 colors (simplified) */
    palette[0][0] = 0; palette[0][1] = 0; palette[0][2] = 0;
    palette[1][0] = 0; palette[1][1] = 0; palette[1][2] = 255;
    palette[2][0] = 255; palette[2][1] = 0; palette[2][2] = 0;
    palette[3][0] = 0; palette[3][1] = 255; palette[3][2] = 0;

    int current_color = 0;
    int x = 0, y = 0;
    int max_x = 0, max_y = 0;

    /* First pass: determine dimensions */
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c >= 0x3F && c <= 0x7E) {
            x++;
            if (x > max_x) max_x = x;
            if (y + 5 > max_y) max_y = y + 5;
        } else if (c == '$') {
            x = 0;
        } else if (c == '-') {
            x = 0;
            y += 6;
        } else if (c == '!') {
            /* Repeat: !N<char> */
            int count = 0;
            i++;
            while (i < len && data[i] >= '0' && data[i] <= '9') {
                count = count * 10 + (data[i] - '0');
                i++;
            }
            if (i < len && data[i] >= 0x3F && data[i] <= 0x7E) {
                x += count;
                if (x > max_x) max_x = x;
                if (y + 5 > max_y) max_y = y + 5;
            }
        }
    }

    if (max_x == 0 || max_y == 0) return false;
    if (max_x > SIXEL_MAX_WIDTH || max_y + 1 > SIXEL_MAX_HEIGHT) return false;

    uint32_t width = (uint32_t)max_x;
    uint32_t height = (uint32_t)(max_y + 1);

    /* Allocate RGBA buffer */
    size_t pixel_count = (size_t)width * height * 4;
    uint8_t *pixels = calloc(pixel_count, 1);
    if (!pixels) return false;

    /* Second pass: render pixels */
    x = 0; y = 0; current_color = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (c == '#') {
            /* Color command */
            i++;
            int reg = 0;
            while (i < len && data[i] >= '0' && data[i] <= '9') {
                reg = reg * 10 + (data[i] - '0');
                i++;
            }
            if (i < len && data[i] == ';') {
                /* Color definition: #N;type;R;G;B */
                i++;
                int type = 0, r = 0, g = 0, b = 0;
                while (i < len && data[i] >= '0' && data[i] <= '9') {
                    type = type * 10 + (data[i] - '0'); i++;
                }
                if (i < len && data[i] == ';') {
                    i++;
                    while (i < len && data[i] >= '0' && data[i] <= '9') {
                        r = r * 10 + (data[i] - '0'); i++;
                    }
                    if (i < len && data[i] == ';') {
                        i++;
                        while (i < len && data[i] >= '0' && data[i] <= '9') {
                            g = g * 10 + (data[i] - '0'); i++;
                        }
                        if (i < len && data[i] == ';') {
                            i++;
                            while (i < len && data[i] >= '0' && data[i] <= '9') {
                                b = b * 10 + (data[i] - '0'); i++;
                            }
                        }
                    }
                }
                if (reg < MAX_SIXEL_COLORS) {
                    if (type == 2) { /* RGB percentage */
                        palette[reg][0] = (uint8_t)(r * 255 / 100);
                        palette[reg][1] = (uint8_t)(g * 255 / 100);
                        palette[reg][2] = (uint8_t)(b * 255 / 100);
                    }
                }
                i--; /* Back up for loop increment */
            } else {
                current_color = reg < MAX_SIXEL_COLORS ? reg : 0;
                i--; /* Back up */
            }
        } else if (c >= 0x3F && c <= 0x7E) {
            /* Sixel data character */
            uint8_t bits = c - 0x3F;
            for (int bit = 0; bit < 6; bit++) {
                if (bits & (1 << bit)) {
                    int py = y + bit;
                    if (x < (int)width && py < (int)height) {
                        size_t off = ((size_t)py * width + (size_t)x) * 4;
                        pixels[off + 0] = palette[current_color][0];
                        pixels[off + 1] = palette[current_color][1];
                        pixels[off + 2] = palette[current_color][2];
                        pixels[off + 3] = 255; /* Opaque */
                    }
                }
            }
            x++;
        } else if (c == '$') {
            x = 0;
        } else if (c == '-') {
            x = 0;
            y += 6;
        } else if (c == '!') {
            /* Repeat */
            int count = 0;
            i++;
            while (i < len && data[i] >= '0' && data[i] <= '9') {
                count = count * 10 + (data[i] - '0');
                i++;
            }
            if (i < len && data[i] >= 0x3F && data[i] <= 0x7E) {
                uint8_t bits = data[i] - 0x3F;
                for (int rep = 0; rep < count; rep++) {
                    for (int bit = 0; bit < 6; bit++) {
                        if (bits & (1 << bit)) {
                            int py = y + bit;
                            if (x < (int)width && py < (int)height) {
                                size_t off = ((size_t)py * width + (size_t)x) * 4;
                                pixels[off + 0] = palette[current_color][0];
                                pixels[off + 1] = palette[current_color][1];
                                pixels[off + 2] = palette[current_color][2];
                                pixels[off + 3] = 255;
                            }
                        }
                    }
                    x++;
                }
            }
        }
    }

    *out_width = width;
    *out_height = height;
    *out_pixels = pixels;
    return true;
}
