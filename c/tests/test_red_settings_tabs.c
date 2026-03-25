/* test_red_settings_tabs.c — RED tests for reorganized 4-tab settings
 *
 * Settings reorganized from 7 tabs to 4:
 * - Appearance: Font + Colors + Window chrome
 * - Terminal: Cursor + Bell + Scrollback + Profiles + Renderer
 * - Accessibility: All a11y + audio settings
 * - Keybindings: Standalone listbox
 *
 * Since PropertySheet is Win32-only and needs a real window,
 * we test the tab configuration data structure instead.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/settings_dialog.h"

TEST red_settings_tab_count(void) {
    ASSERT_EQ(4, wixen_settings_tab_count());
    PASS();
}

TEST red_settings_tab_names(void) {
    ASSERT_STR_EQ("Appearance", wixen_settings_tab_name(0));
    ASSERT_STR_EQ("Terminal", wixen_settings_tab_name(1));
    ASSERT_STR_EQ("Accessibility", wixen_settings_tab_name(2));
    ASSERT_STR_EQ("Keybindings", wixen_settings_tab_name(3));
    PASS();
}

TEST red_settings_tab_out_of_range(void) {
    ASSERT(wixen_settings_tab_name(99) == NULL);
    PASS();
}

TEST red_settings_appearance_fields(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(0, &count);
    ASSERT(fields != NULL);
    ASSERT(count >= 8); /* Font family, size, line height, theme, fg, bg, opacity, dark titlebar */
    /* Verify key fields are present */
    bool has_font = false, has_theme = false, has_opacity = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "font") || strstr(fields[i], "Font")) has_font = true;
        if (strstr(fields[i], "theme") || strstr(fields[i], "Theme")) has_theme = true;
        if (strstr(fields[i], "opacity") || strstr(fields[i], "Opacity")) has_opacity = true;
    }
    ASSERT(has_font);
    ASSERT(has_theme);
    ASSERT(has_opacity);
    PASS();
}

TEST red_settings_terminal_fields(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(1, &count);
    ASSERT(fields != NULL);
    ASSERT(count >= 6); /* Cursor style, blink, bell, scrollback, profiles, renderer */
    bool has_cursor = false, has_bell = false, has_profile = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "cursor") || strstr(fields[i], "Cursor")) has_cursor = true;
        if (strstr(fields[i], "bell") || strstr(fields[i], "Bell")) has_bell = true;
        if (strstr(fields[i], "profile") || strstr(fields[i], "Profile")) has_profile = true;
    }
    ASSERT(has_cursor);
    ASSERT(has_bell);
    ASSERT(has_profile);
    PASS();
}

TEST red_settings_a11y_fields(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(2, &count);
    ASSERT(fields != NULL);
    ASSERT(count >= 6); /* Verbosity, debounce, exit codes, prompt, motion, audio */
    bool has_verbosity = false, has_audio = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "verbosity") || strstr(fields[i], "Verbosity")) has_verbosity = true;
        if (strstr(fields[i], "audio") || strstr(fields[i], "Audio")) has_audio = true;
    }
    ASSERT(has_verbosity);
    ASSERT(has_audio);
    PASS();
}

TEST red_settings_keybindings_fields(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(3, &count);
    ASSERT(fields != NULL);
    ASSERT(count >= 1); /* At least the keybinding list */
    PASS();
}

SUITE(red_settings_tabs) {
    RUN_TEST(red_settings_tab_count);
    RUN_TEST(red_settings_tab_names);
    RUN_TEST(red_settings_tab_out_of_range);
    RUN_TEST(red_settings_appearance_fields);
    RUN_TEST(red_settings_terminal_fields);
    RUN_TEST(red_settings_a11y_fields);
    RUN_TEST(red_settings_keybindings_fields);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_settings_tabs);
    GREATEST_MAIN_END();
}
