/* test_red_ui_elements.c — RED tests for all UI element quality
 *
 * Every UI element must:
 * - Use Segoe UI 9pt (consistent with settings dialog)
 * - Have accessible labels for screen readers
 * - Be keyboard navigable
 * - Have sensible defaults
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/palette_dialog.h"
#include "wixen/ui/window.h"
#include "wixen/ui/tray.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* === Command Palette === */

TEST red_palette_item_count(void) {
    size_t count = 0;
    const WixenPaletteEntry *entries = wixen_palette_default_entries(&count);
    ASSERT(entries != NULL);
    ASSERT(count >= 8); /* Settings, New Tab, Close Tab, Split, Search, etc. */
    PASS();
}

TEST red_palette_entries_have_labels(void) {
    size_t count = 0;
    const WixenPaletteEntry *entries = wixen_palette_default_entries(&count);
    for (size_t i = 0; i < count; i++) {
        ASSERT(entries[i].label != NULL);
        ASSERT(strlen(entries[i].label) > 0);
        ASSERT(entries[i].action != NULL);
        ASSERT(strlen(entries[i].action) > 0);
    }
    PASS();
}

TEST red_palette_entries_have_shortcuts(void) {
    size_t count = 0;
    const WixenPaletteEntry *entries = wixen_palette_default_entries(&count);
    /* At least some entries should have keyboard shortcuts shown */
    int with_shortcut = 0;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].shortcut && strlen(entries[i].shortcut) > 0)
            with_shortcut++;
    }
    ASSERT(with_shortcut >= 5); /* Most common actions have shortcuts */
    PASS();
}

TEST red_palette_font(void) {
    const char *font = wixen_palette_font_name();
    ASSERT_STR_EQ("Segoe UI", font);
    int size = wixen_palette_font_size();
    ASSERT(size >= 9 && size <= 11); /* 9-11pt range */
    PASS();
}

/* === Context Menu === */

TEST red_context_menu_items(void) {
    size_t count = 0;
    const WixenContextMenuItem *items = wixen_context_menu_items(&count);
    ASSERT(items != NULL);
    ASSERT(count >= 6); /* Copy, Paste, Select All, Search, Settings, separator(s) */
    PASS();
}

TEST red_context_menu_labels(void) {
    size_t count = 0;
    const WixenContextMenuItem *items = wixen_context_menu_items(&count);
    for (size_t i = 0; i < count; i++) {
        if (!items[i].is_separator) {
            ASSERT(items[i].label != NULL);
            ASSERT(strlen(items[i].label) > 0);
        }
    }
    PASS();
}

TEST red_context_menu_has_copy_paste(void) {
    size_t count = 0;
    const WixenContextMenuItem *items = wixen_context_menu_items(&count);
    bool has_copy = false, has_paste = false, has_settings = false;
    for (size_t i = 0; i < count; i++) {
        if (items[i].label && strstr(items[i].label, "Copy")) has_copy = true;
        if (items[i].label && strstr(items[i].label, "Paste")) has_paste = true;
        if (items[i].label && strstr(items[i].label, "Settings")) has_settings = true;
    }
    ASSERT(has_copy);
    ASSERT(has_paste);
    ASSERT(has_settings);
    PASS();
}

/* === Tray Icon Menu === */

TEST red_tray_menu_items(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    ASSERT(items != NULL);
    ASSERT(count >= 3); /* Show, Settings, Quit */
    PASS();
}

TEST red_tray_menu_has_quit(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    bool has_show = false, has_quit = false;
    for (size_t i = 0; i < count; i++) {
        if (items[i].label && strstr(items[i].label, "Show")) has_show = true;
        if (items[i].label && strstr(items[i].label, "Quit")) has_quit = true;
    }
    ASSERT(has_show);
    ASSERT(has_quit);
    PASS();
}

/* === Window Title === */

TEST red_window_default_title(void) {
    const char *title = wixen_window_default_title();
    ASSERT(title != NULL);
    ASSERT(strstr(title, "Wixen") != NULL);
    PASS();
}

TEST red_window_title_with_tab(void) {
    char *title = wixen_window_format_title("PowerShell", NULL);
    ASSERT(title != NULL);
    ASSERT(strstr(title, "PowerShell") != NULL);
    ASSERT(strstr(title, "Wixen") != NULL);
    free(title);
    PASS();
}

TEST red_window_title_with_cwd(void) {
    char *title = wixen_window_format_title("Shell", "C:\\Projects");
    ASSERT(title != NULL);
    ASSERT(strstr(title, "C:\\Projects") != NULL || strstr(title, "Projects") != NULL);
    free(title);
    PASS();
}

SUITE(red_ui_elements) {
    /* Palette */
    RUN_TEST(red_palette_item_count);
    RUN_TEST(red_palette_entries_have_labels);
    RUN_TEST(red_palette_entries_have_shortcuts);
    RUN_TEST(red_palette_font);
    /* Context menu */
    RUN_TEST(red_context_menu_items);
    RUN_TEST(red_context_menu_labels);
    RUN_TEST(red_context_menu_has_copy_paste);
    /* Tray */
    RUN_TEST(red_tray_menu_items);
    RUN_TEST(red_tray_menu_has_quit);
    /* Window title */
    RUN_TEST(red_window_default_title);
    RUN_TEST(red_window_title_with_tab);
    RUN_TEST(red_window_title_with_cwd);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_ui_elements);
    GREATEST_MAIN_END();
}
