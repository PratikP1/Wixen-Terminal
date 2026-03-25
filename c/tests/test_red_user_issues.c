/* test_red_user_issues.c — RED tests for user-reported issues
 *
 * 1. Alt+F4 must close window
 * 2. Context menu first item gets focus
 * 3. Keybindings are editable in settings
 * 4. Tray icon has functional right-click menu
 * 5. System menu (Alt+Space) includes Settings
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/window.h"
#include "wixen/ui/tray.h"
#include "wixen/ui/settings_dialog.h"
#include "wixen/config/keybindings.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* === Issue 1: Alt+F4 closes window === */

TEST red_alt_f4_in_keybindings(void) {
    /* Alt+F4 should NOT be intercepted by our keybinding system.
     * It must pass through to DefWindowProc which handles WM_CLOSE.
     * Verify it's not mapped to any action. */
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *action = wixen_keybindings_lookup(&kb, "alt+f4");
    /* alt+f4 should NOT be in our bindings — let Windows handle it */
    ASSERT(action == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

/* === Issue 2: Context menu first item focused === */

TEST red_context_menu_first_item_not_separator(void) {
    /* First item must be actionable, not a separator */
    size_t count = 0;
    const WixenContextMenuItem *items = wixen_context_menu_items(&count);
    ASSERT(count > 0);
    ASSERT_FALSE(items[0].is_separator);
    ASSERT(items[0].label != NULL);
    PASS();
}

TEST red_context_menu_has_focus_first_flag(void) {
    /* The context menu config should indicate focus goes to first item.
     * Win32 TrackPopupMenu with TPM_TOPALIGN focuses first item. */
    ASSERT(wixen_context_menu_focus_first());
    PASS();
}

/* === Issue 3: Keybindings are editable === */

TEST red_keybindings_add_custom(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    size_t before = kb.count;
    wixen_keybindings_add(&kb, "ctrl+shift+q", "quit", "Quit");
    ASSERT_EQ(before + 1, kb.count);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+shift+q");
    ASSERT_STR_EQ("quit", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_keybindings_remove(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    /* Add then remove */
    wixen_keybindings_add(&kb, "ctrl+shift+q", "quit", "Quit");
    bool removed = wixen_keybindings_remove(&kb, "ctrl+shift+q");
    ASSERT(removed);
    ASSERT(wixen_keybindings_lookup(&kb, "ctrl+shift+q") == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_keybindings_modify(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "f5", "refresh", "Refresh");
    /* Modify: change the action for an existing chord */
    wixen_keybindings_add(&kb, "f5", "rebuild", "Rebuild");
    const char *action = wixen_keybindings_lookup(&kb, "f5");
    ASSERT_STR_EQ("rebuild", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_keybindings_tab_is_editable(void) {
    /* The keybindings tab metadata should indicate editability */
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(3, &count);
    ASSERT(fields != NULL);
    /* Should have edit-related fields */
    bool has_list = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "list") || strstr(fields[i], "List") ||
            strstr(fields[i], "binding") || strstr(fields[i], "Binding"))
            has_list = true;
    }
    ASSERT(has_list);
    PASS();
}

/* === Issue 4: Tray icon has functional menu === */

TEST red_tray_menu_has_all_actions(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    ASSERT(count >= 4); /* Show, New Tab, Settings, Quit */
    bool has_show = false, has_new_tab = false, has_settings = false, has_quit = false;
    for (size_t i = 0; i < count; i++) {
        if (items[i].label && strstr(items[i].label, "Show")) has_show = true;
        if (items[i].label && strstr(items[i].label, "New Tab")) has_new_tab = true;
        if (items[i].label && strstr(items[i].label, "Settings")) has_settings = true;
        if (items[i].label && strstr(items[i].label, "Quit")) has_quit = true;
    }
    ASSERT(has_show);
    ASSERT(has_new_tab);
    ASSERT(has_settings);
    ASSERT(has_quit);
    PASS();
}

TEST red_tray_menu_action_ids_valid(void) {
    size_t count = 0;
    const WixenTrayMenuItem *items = wixen_tray_menu_items(&count);
    for (size_t i = 0; i < count; i++) {
        ASSERT(items[i].action_id > 0); /* 0 = invalid */
    }
    PASS();
}

/* === Issue 5: System menu includes Settings === */

TEST red_system_menu_has_settings(void) {
    size_t count = 0;
    const WixenSystemMenuItem *items = wixen_system_menu_items(&count);
    ASSERT(items != NULL);
    ASSERT(count >= 1);
    bool has_settings = false;
    for (size_t i = 0; i < count; i++) {
        if (items[i].label && strstr(items[i].label, "Settings")) has_settings = true;
    }
    ASSERT(has_settings);
    PASS();
}

SUITE(red_user_issues) {
    RUN_TEST(red_alt_f4_in_keybindings);
    RUN_TEST(red_context_menu_first_item_not_separator);
    RUN_TEST(red_context_menu_has_focus_first_flag);
    RUN_TEST(red_keybindings_add_custom);
    RUN_TEST(red_keybindings_remove);
    RUN_TEST(red_keybindings_modify);
    RUN_TEST(red_keybindings_tab_is_editable);
    RUN_TEST(red_tray_menu_has_all_actions);
    RUN_TEST(red_tray_menu_action_ids_valid);
    RUN_TEST(red_system_menu_has_settings);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_user_issues);
    GREATEST_MAIN_END();
}
