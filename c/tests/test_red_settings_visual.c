/* test_red_settings_visual.c — RED tests for settings visual quality
 *
 * A modern Win32 dialog needs:
 * - Segoe UI 9pt font (not MS Sans Serif / DEFAULT_GUI_FONT)
 * - Adequate spacing (DLU-based, not pixel-hardcoded)
 * - Minimum dialog size to prevent control truncation
 * - Consistent margins and padding
 * - Group boxes for logical sections
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/settings_dialog.h"

/* --- Font specification --- */

TEST red_dialog_font_name(void) {
    const char *font = wixen_settings_dialog_font_name();
    ASSERT(font != NULL);
    ASSERT_STR_EQ("Segoe UI", font);
    PASS();
}

TEST red_dialog_font_size(void) {
    int size = wixen_settings_dialog_font_size();
    ASSERT_EQ(9, size); /* 9pt = Windows standard for dialogs */
    PASS();
}

/* --- Dialog dimensions --- */

TEST red_dialog_min_width(void) {
    int w = wixen_settings_dialog_width();
    ASSERT(w >= 420); /* Enough for label + control + margin */
    PASS();
}

TEST red_dialog_min_height(void) {
    int h = wixen_settings_dialog_height();
    ASSERT(h >= 320); /* Enough for all controls without scrolling */
    PASS();
}

/* --- Spacing constants --- */

TEST red_dialog_margins(void) {
    int margin = wixen_settings_dialog_margin();
    ASSERT(margin >= 7); /* Windows UX guideline: 7 DLU minimum */
    ASSERT(margin <= 14); /* Not excessive */
    PASS();
}

TEST red_dialog_control_spacing(void) {
    int spacing = wixen_settings_dialog_control_spacing();
    ASSERT(spacing >= 4); /* Minimum between controls */
    ASSERT(spacing <= 8); /* Not too spread out */
    PASS();
}

/* --- Appearance tab has group boxes --- */

TEST red_appearance_has_groups(void) {
    size_t count = 0;
    const char **groups = wixen_settings_appearance_groups(&count);
    ASSERT(groups != NULL);
    ASSERT(count == 3); /* Font, Colors, Window */
    ASSERT_STR_EQ("Font", groups[0]);
    ASSERT_STR_EQ("Colors", groups[1]);
    ASSERT_STR_EQ("Window", groups[2]);
    PASS();
}

SUITE(red_settings_visual) {
    RUN_TEST(red_dialog_font_name);
    RUN_TEST(red_dialog_font_size);
    RUN_TEST(red_dialog_min_width);
    RUN_TEST(red_dialog_min_height);
    RUN_TEST(red_dialog_margins);
    RUN_TEST(red_dialog_control_spacing);
    RUN_TEST(red_appearance_has_groups);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_settings_visual);
    GREATEST_MAIN_END();
}
