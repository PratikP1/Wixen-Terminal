/* test_colors.c — Tests for color scheme */
#include <stdbool.h>
#include <math.h>
#include "greatest.h"
#include "wixen/render/colors.h"

#define APPROX(a, b) (fabs((double)(a) - (double)(b)) < 0.005)

TEST colors_default_init(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Check default fg/bg */
    ASSERT_EQ(204, cs.default_fg.r);
    ASSERT_EQ(204, cs.default_fg.g);
    ASSERT_EQ(204, cs.default_fg.b);
    ASSERT_EQ(12, cs.default_bg.r);
    PASS();
}

TEST colors_ansi_16(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Black */
    ASSERT_EQ(12, cs.palette[0].r);
    /* Red */
    ASSERT_EQ(197, cs.palette[1].r);
    /* Bright White */
    ASSERT_EQ(242, cs.palette[15].r);
    PASS();
}

TEST colors_216_cube(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 16 = (0,0,0) → black */
    ASSERT_EQ(0, cs.palette[16].r);
    ASSERT_EQ(0, cs.palette[16].g);
    ASSERT_EQ(0, cs.palette[16].b);
    /* Index 196 = (5,0,0) → bright red */
    WixenRgb red = cs.palette[196];
    ASSERT_EQ(255, red.r);
    ASSERT_EQ(0, red.g);
    ASSERT_EQ(0, red.b);
    /* Index 231 = (5,5,5) → white */
    ASSERT_EQ(255, cs.palette[231].r);
    ASSERT_EQ(255, cs.palette[231].g);
    ASSERT_EQ(255, cs.palette[231].b);
    PASS();
}

TEST colors_grayscale(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    /* Index 232 = darkest gray (8) */
    ASSERT_EQ(8, cs.palette[232].r);
    ASSERT_EQ(8, cs.palette[232].g);
    /* Index 255 = lightest gray (238) */
    ASSERT_EQ(238, cs.palette[255].r);
    PASS();
}

TEST colors_resolve_indexed(void) {
    WixenColorScheme cs;
    wixen_colors_init_default(&cs);
    WixenRgb c = wixen_colors_resolve_indexed(&cs, 1);
    ASSERT_EQ(197, c.r); /* Red */
    PASS();
}

TEST colors_rgb_to_float(void) {
    float f[3];
    wixen_rgb_to_float((WixenRgb){255, 128, 0}, f);
    ASSERT(APPROX(1.0, f[0]));
    ASSERT(APPROX(0.502, f[1]));
    ASSERT(APPROX(0.0, f[2]));
    PASS();
}

TEST colors_rgb_to_float_black(void) {
    float f[3];
    wixen_rgb_to_float((WixenRgb){0, 0, 0}, f);
    ASSERT(APPROX(0, f[0]));
    ASSERT(APPROX(0, f[1]));
    ASSERT(APPROX(0, f[2]));
    PASS();
}

SUITE(color_scheme) {
    RUN_TEST(colors_default_init);
    RUN_TEST(colors_ansi_16);
    RUN_TEST(colors_216_cube);
    RUN_TEST(colors_grayscale);
    RUN_TEST(colors_resolve_indexed);
    RUN_TEST(colors_rgb_to_float);
    RUN_TEST(colors_rgb_to_float_black);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(color_scheme);
    GREATEST_MAIN_END();
}
