/* test_colors_extended.c — Color conversion and scheme tests */
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "greatest.h"
#include "wixen/render/colors.h"

TEST color_init_default_not_zero(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Default FG should not be all-zero (black on black) */
    ASSERT(cs.default_fg.r > 0 || cs.default_fg.g > 0 || cs.default_fg.b > 0);
    PASS();
}

TEST color_rgb_to_float_white(void) {
    WixenRgb white = { 255, 255, 255 };
    float f[3];
    wixen_rgb_to_float(white, f);
    ASSERT(fabsf(f[0] - 1.0f) < 0.01f);
    ASSERT(fabsf(f[1] - 1.0f) < 0.01f);
    ASSERT(fabsf(f[2] - 1.0f) < 0.01f);
    PASS();
}

TEST color_rgb_to_float_black(void) {
    WixenRgb black = { 0, 0, 0 };
    float f[3];
    wixen_rgb_to_float(black, f);
    ASSERT(fabsf(f[0]) < 0.01f);
    ASSERT(fabsf(f[1]) < 0.01f);
    ASSERT(fabsf(f[2]) < 0.01f);
    PASS();
}

TEST color_rgb_to_float_red(void) {
    WixenRgb red = { 255, 0, 0 };
    float f[3];
    wixen_rgb_to_float(red, f);
    ASSERT(fabsf(f[0] - 1.0f) < 0.01f);
    ASSERT(fabsf(f[1]) < 0.01f);
    ASSERT(fabsf(f[2]) < 0.01f);
    PASS();
}

TEST color_palette_first_16(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 0 (black) and index 7 (white) should differ */
    ASSERT(cs.palette[0].r != cs.palette[7].r ||
           cs.palette[0].g != cs.palette[7].g ||
           cs.palette[0].b != cs.palette[7].b);
    PASS();
}

TEST color_palette_256_cube(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 16 = (0,0,0), Index 231 = (5,5,5) mapped to RGB */
    ASSERT_EQ(0, cs.palette[16].r);
    ASSERT_EQ(0, cs.palette[16].g);
    ASSERT_EQ(0, cs.palette[16].b);
    ASSERT_EQ(255, cs.palette[231].r);
    ASSERT_EQ(255, cs.palette[231].g);
    ASSERT_EQ(255, cs.palette[231].b);
    PASS();
}

TEST color_palette_grayscale_ramp(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* 232 should be darker than 255 */
    ASSERT(cs.palette[232].r < cs.palette[255].r);
    PASS();
}

TEST color_default_bg_dark(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Default BG should be dark (terminal convention) */
    int sum = cs.default_bg.r + cs.default_bg.g + cs.default_bg.b;
    ASSERT(sum < 384); /* Less than 50% brightness */
    PASS();
}

TEST color_default_fg_light(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Default FG should be light */
    int sum = cs.default_fg.r + cs.default_fg.g + cs.default_fg.b;
    ASSERT(sum > 192); /* More than 25% brightness */
    PASS();
}

TEST color_rgb_components(void) {
    WixenRgb c = { 0xAA, 0xBB, 0xCC };
    ASSERT_EQ(0xAA, c.r);
    ASSERT_EQ(0xBB, c.g);
    ASSERT_EQ(0xCC, c.b);
    PASS();
}

SUITE(colors_extended) {
    RUN_TEST(color_init_default_not_zero);
    RUN_TEST(color_rgb_to_float_white);
    RUN_TEST(color_rgb_to_float_black);
    RUN_TEST(color_rgb_to_float_red);
    RUN_TEST(color_palette_first_16);
    RUN_TEST(color_palette_256_cube);
    RUN_TEST(color_palette_grayscale_ramp);
    RUN_TEST(color_default_bg_dark);
    RUN_TEST(color_default_fg_light);
    RUN_TEST(color_rgb_components);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(colors_extended);
    GREATEST_MAIN_END();
}
