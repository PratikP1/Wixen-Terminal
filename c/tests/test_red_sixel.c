/* test_red_sixel.c — RED tests for Sixel image protocol
 *
 * Sixel encodes pixel data in 6-row bands using printable ASCII.
 * The decoder produces an RGBA pixel buffer.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/sixel.h"

TEST red_sixel_decode_single_pixel(void) {
    /* Simplest sixel: one character = 6 vertical pixels
     * '?' = 0x3F = all 6 bits off (blank)
     * '@' = 0x40 - 0x3F = 1 = bottom bit on */
    WixenSixelImage img;
    /* DCS q <data> ST  — we pass just the data portion */
    ASSERT(wixen_sixel_decode("@", 1, &img));
    ASSERT(img.width >= 1);
    ASSERT(img.height >= 1);
    wixen_sixel_free(&img);
    PASS();
}

TEST red_sixel_decode_color_intro(void) {
    /* #0;2;100;0;0 sets color 0 to RGB(100%,0%,0%) = red
     * Then '~' = all 6 bits on (solid) */
    const char *data = "#0;2;100;0;0~";
    WixenSixelImage img;
    ASSERT(wixen_sixel_decode(data, strlen(data), &img));
    /* First pixel column should be red (or close) */
    ASSERT(img.width >= 1);
    ASSERT(img.height >= 1);
    if (img.pixels) {
        /* RGBA: R should be 255 (100%) */
        ASSERT(img.pixels[0] > 200);
    }
    wixen_sixel_free(&img);
    PASS();
}

TEST red_sixel_decode_newline(void) {
    /* '-' is the sixel newline (move to next 6-row band) */
    const char *data = "~-~";
    WixenSixelImage img;
    ASSERT(wixen_sixel_decode(data, strlen(data), &img));
    ASSERT(img.height >= 12); /* At least 2 bands of 6 */
    wixen_sixel_free(&img);
    PASS();
}

TEST red_sixel_decode_empty(void) {
    WixenSixelImage img;
    ASSERT_FALSE(wixen_sixel_decode("", 0, &img));
    PASS();
}

TEST red_sixel_repeat(void) {
    /* !<count><char> repeats a sixel character */
    const char *data = "!5~"; /* 5 columns of all-on */
    WixenSixelImage img;
    ASSERT(wixen_sixel_decode(data, strlen(data), &img));
    ASSERT(img.width >= 5);
    wixen_sixel_free(&img);
    PASS();
}

SUITE(red_sixel) {
    RUN_TEST(red_sixel_decode_single_pixel);
    RUN_TEST(red_sixel_decode_color_intro);
    RUN_TEST(red_sixel_decode_newline);
    RUN_TEST(red_sixel_decode_empty);
    RUN_TEST(red_sixel_repeat);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_sixel);
    GREATEST_MAIN_END();
}
