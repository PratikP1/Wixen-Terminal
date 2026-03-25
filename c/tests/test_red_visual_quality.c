/* test_red_visual_quality.c — RED tests for visual quality
 *
 * 1. Font must scale with DPI
 * 2. Dialog must be tall enough for all content
 * 3. Font handle must be shared (not leaked per dialog proc)
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/settings_dialog.h"

/* === Font scales with DPI === */

TEST red_font_height_at_96dpi(void) {
    int h = wixen_settings_dialog_font_height(96);
    /* 9pt at 96 DPI = -MulDiv(9, 96, 72) = -12 */
    ASSERT_EQ(-12, h);
    PASS();
}

TEST red_font_height_at_144dpi(void) {
    int h = wixen_settings_dialog_font_height(144);
    /* 9pt at 144 DPI = -MulDiv(9, 144, 72) = -18 */
    ASSERT_EQ(-18, h);
    PASS();
}

TEST red_font_height_at_192dpi(void) {
    int h = wixen_settings_dialog_font_height(192);
    /* 9pt at 192 DPI = -MulDiv(9, 192, 72) = -24 */
    ASSERT_EQ(-24, h);
    PASS();
}

/* === Dialog tall enough === */

TEST red_dialog_height_fits_appearance(void) {
    int h = wixen_settings_dialog_height();
    /* Appearance has 3 groups: Font(~80) + Colors(~75) + Window(~75) + margins = ~260 min */
    ASSERT(h >= 340);
    PASS();
}

/* === Tab count correct after all changes === */

TEST red_tab_count_still_4(void) {
    ASSERT_EQ(4, wixen_settings_tab_count());
    PASS();
}

/* === Keybindings has 4 fields === */

TEST red_kb_field_count(void) {
    size_t count = 0;
    wixen_settings_tab_fields(3, &count);
    ASSERT_EQ(4, (int)count); /* list, add, remove, edit */
    PASS();
}

SUITE(red_visual_quality) {
    RUN_TEST(red_font_height_at_96dpi);
    RUN_TEST(red_font_height_at_144dpi);
    RUN_TEST(red_font_height_at_192dpi);
    RUN_TEST(red_dialog_height_fits_appearance);
    RUN_TEST(red_tab_count_still_4);
    RUN_TEST(red_kb_field_count);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_visual_quality);
    GREATEST_MAIN_END();
}
