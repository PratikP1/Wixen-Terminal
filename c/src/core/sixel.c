/* sixel.c — Sixel image protocol decoder
 *
 * Sixel encodes pixel data in 6-row vertical bands.
 * Characters 0x3F-0x7E map to 6-bit patterns (subtract 0x3F).
 * '#' introduces color definitions/selections.
 * '-' moves to the next band (6 rows down).
 * '$' returns to column 0 in the current band.
 * '!' starts a repeat count.
 */
#include "wixen/core/sixel.h"
#include <stdlib.h>
#include <string.h>

#define MAX_COLORS 256
#define INITIAL_WIDTH 256
#define BAND_HEIGHT 6

typedef struct {
    uint8_t r, g, b;
} SixelColor;

bool wixen_sixel_decode(const char *data, size_t len, WixenSixelImage *out) {
    if (!data || len == 0 || !out) return false;
    memset(out, 0, sizeof(*out));

    /* Allocate initial canvas */
    uint32_t canvas_w = INITIAL_WIDTH;
    uint32_t canvas_h = BAND_HEIGHT;
    uint8_t *pixels = calloc((size_t)canvas_w * canvas_h, 4);
    if (!pixels) return false;

    SixelColor palette[MAX_COLORS];
    memset(palette, 0, sizeof(palette));
    /* Default palette: color 0 = white */
    palette[0] = (SixelColor){255, 255, 255};

    int cur_color = 0;
    uint32_t x = 0;
    uint32_t band_y = 0; /* Top row of current band */
    uint32_t max_x = 0;

    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        if (c == '#') {
            /* Color introduction: #<idx> or #<idx>;2;<r>;<g>;<b> */
            i++;
            int idx = 0;
            while (i < len && data[i] >= '0' && data[i] <= '9') {
                idx = idx * 10 + (data[i] - '0');
                i++;
            }
            if (idx >= MAX_COLORS) idx = 0;
            if (i < len && data[i] == ';') {
                i++; /* Skip ';' */
                int mode = 0;
                while (i < len && data[i] >= '0' && data[i] <= '9') {
                    mode = mode * 10 + (data[i] - '0');
                    i++;
                }
                if (mode == 2 && i < len && data[i] == ';') {
                    /* RGB percentages */
                    int rgb[3] = {0, 0, 0};
                    for (int j = 0; j < 3; j++) {
                        i++; /* Skip ';' */
                        rgb[j] = 0;
                        while (i < len && data[i] >= '0' && data[i] <= '9') {
                            rgb[j] = rgb[j] * 10 + (data[i] - '0');
                            i++;
                        }
                        if (j < 2 && i < len && data[i] == ';') continue;
                    }
                    palette[idx].r = (uint8_t)(rgb[0] * 255 / 100);
                    palette[idx].g = (uint8_t)(rgb[1] * 255 / 100);
                    palette[idx].b = (uint8_t)(rgb[2] * 255 / 100);
                }
            }
            cur_color = idx;
            i--; /* Will be incremented by for loop */
            continue;
        }

        if (c == '-') {
            /* Next band */
            band_y += BAND_HEIGHT;
            x = 0;
            /* Grow canvas height if needed */
            if (band_y + BAND_HEIGHT > canvas_h) {
                uint32_t new_h = band_y + BAND_HEIGHT;
                uint8_t *new_pix = realloc(pixels, (size_t)canvas_w * new_h * 4);
                if (!new_pix) { free(pixels); return false; }
                memset(new_pix + (size_t)canvas_w * canvas_h * 4, 0,
                       (size_t)canvas_w * (new_h - canvas_h) * 4);
                pixels = new_pix;
                canvas_h = new_h;
            }
            continue;
        }

        if (c == '$') {
            x = 0;
            continue;
        }

        /* Repeat count */
        int repeat = 1;
        if (c == '!') {
            i++;
            repeat = 0;
            while (i < len && data[i] >= '0' && data[i] <= '9') {
                repeat = repeat * 10 + (data[i] - '0');
                i++;
            }
            if (repeat <= 0) repeat = 1;
            if (i >= len) break;
            c = data[i];
        }

        /* Sixel data character */
        if (c >= '?' && c <= '~') {
            uint8_t bits = (uint8_t)(c - '?');
            SixelColor col = palette[cur_color];

            for (int r = 0; r < repeat; r++) {
                if (x >= canvas_w) {
                    /* Grow width */
                    uint32_t new_w = canvas_w * 2;
                    uint8_t *new_pix = calloc((size_t)new_w * canvas_h, 4);
                    if (!new_pix) { free(pixels); return false; }
                    for (uint32_t row = 0; row < canvas_h; row++) {
                        memcpy(new_pix + (size_t)row * new_w * 4,
                               pixels + (size_t)row * canvas_w * 4,
                               (size_t)canvas_w * 4);
                    }
                    free(pixels);
                    pixels = new_pix;
                    canvas_w = new_w;
                }
                /* Set 6 vertical pixels */
                for (int bit = 0; bit < 6; bit++) {
                    if (bits & (1 << bit)) {
                        uint32_t py = band_y + (uint32_t)bit;
                        if (py < canvas_h) {
                            size_t offset = ((size_t)py * canvas_w + x) * 4;
                            pixels[offset + 0] = col.r;
                            pixels[offset + 1] = col.g;
                            pixels[offset + 2] = col.b;
                            pixels[offset + 3] = 255;
                        }
                    }
                }
                x++;
                if (x > max_x) max_x = x;
            }
        }
    }

    out->pixels = pixels;
    out->width = max_x > 0 ? max_x : 1;
    out->height = band_y + BAND_HEIGHT;
    out->stride = canvas_w * 4;
    return true;
}

void wixen_sixel_free(WixenSixelImage *img) {
    if (img) {
        free(img->pixels);
        img->pixels = NULL;
        img->width = img->height = 0;
    }
}
