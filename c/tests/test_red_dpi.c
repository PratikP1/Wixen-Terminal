/* test_red_dpi.c — RED tests for DPI awareness
 *
 * When DPI changes (monitor switch, user setting), font metrics must
 * rescale and the grid must recalculate cols/rows.
 */
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "greatest.h"
#include "wixen/render/renderer.h"

TEST red_dpi_font_scale_factor(void) {
    /* At 96 DPI (100%), scale factor should be 1.0 */
    float scale = wixen_dpi_scale_factor(96);
    ASSERT_IN_RANGE(1.0f, scale, 0.01f);
    /* At 144 DPI (150%), scale factor should be 1.5 */
    scale = wixen_dpi_scale_factor(144);
    ASSERT_IN_RANGE(1.5f, scale, 0.01f);
    /* At 192 DPI (200%), scale factor should be 2.0 */
    scale = wixen_dpi_scale_factor(192);
    ASSERT_IN_RANGE(2.0f, scale, 0.01f);
    PASS();
}

TEST red_dpi_zero_returns_one(void) {
    /* Zero DPI should default to 1.0 */
    float scale = wixen_dpi_scale_factor(0);
    ASSERT_IN_RANGE(1.0f, scale, 0.01f);
    PASS();
}

TEST red_dpi_grid_dimensions(void) {
    /* Given a window 1200x800 pixels, cell 8x16 at 96 DPI:
     * cols = 1200/8 = 150, rows = 800/16 = 50
     * At 144 DPI (1.5x): cell = 12x24
     * cols = 1200/12 = 100, rows = 800/24 = 33 */
    uint32_t cols96, rows96, cols144, rows144;
    wixen_dpi_grid_dimensions(1200, 800, 8.0f, 16.0f, 96, &cols96, &rows96);
    ASSERT_EQ(150, (int)cols96);
    ASSERT_EQ(50, (int)rows96);
    wixen_dpi_grid_dimensions(1200, 800, 8.0f, 16.0f, 144, &cols144, &rows144);
    ASSERT_EQ(100, (int)cols144);
    ASSERT(rows144 < rows96);
    PASS();
}

SUITE(red_dpi) {
    RUN_TEST(red_dpi_font_scale_factor);
    RUN_TEST(red_dpi_zero_returns_one);
    RUN_TEST(red_dpi_grid_dimensions);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_dpi);
    GREATEST_MAIN_END();
}
