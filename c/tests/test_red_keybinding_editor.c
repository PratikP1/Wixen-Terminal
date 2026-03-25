/* test_red_keybinding_editor.c — RED tests for keybinding editor UI
 *
 * The keybindings tab must have:
 * - A list of current bindings
 * - Add button to create new binding
 * - Remove button to delete selected binding
 * - Edit/Change button to modify chord for selected binding
 * - All buttons labeled and keyboard accessible
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/settings_dialog.h"
#include "wixen/ui/window.h"

/* === Keybindings tab fields include edit controls === */

TEST red_kb_tab_has_list(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(3, &count);
    ASSERT(fields != NULL);
    bool has_list = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "list") || strstr(fields[i], "List"))
            has_list = true;
    }
    ASSERT(has_list);
    PASS();
}

TEST red_kb_tab_has_add_button(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(3, &count);
    bool has_add = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "Add") || strstr(fields[i], "add"))
            has_add = true;
    }
    ASSERT(has_add);
    PASS();
}

TEST red_kb_tab_has_remove_button(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(3, &count);
    bool has_remove = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "Remove") || strstr(fields[i], "remove") ||
            strstr(fields[i], "Delete") || strstr(fields[i], "delete"))
            has_remove = true;
    }
    ASSERT(has_remove);
    PASS();
}

TEST red_kb_tab_has_edit_button(void) {
    size_t count = 0;
    const char **fields = wixen_settings_tab_fields(3, &count);
    bool has_edit = false;
    for (size_t i = 0; i < count; i++) {
        if (strstr(fields[i], "Edit") || strstr(fields[i], "edit") ||
            strstr(fields[i], "Change") || strstr(fields[i], "change"))
            has_edit = true;
    }
    ASSERT(has_edit);
    PASS();
}

/* === System menu Settings dispatches correct event === */

TEST red_system_menu_settings_action(void) {
    size_t count = 0;
    const WixenSystemMenuItem *items = wixen_system_menu_items(&count);
    bool has_settings = false;
    for (size_t i = 0; i < count; i++) {
        if (items[i].action && strcmp(items[i].action, "settings") == 0)
            has_settings = true;
    }
    ASSERT(has_settings);
    PASS();
}

/* === Context menu Settings is wirable === */

TEST red_context_settings_action(void) {
    size_t count = 0;
    const WixenContextMenuItem *items = wixen_context_menu_items(&count);
    bool found = false;
    for (size_t i = 0; i < count; i++) {
        if (items[i].action && strcmp(items[i].action, "settings") == 0)
            found = true;
    }
    ASSERT(found);
    PASS();
}

SUITE(red_keybinding_editor) {
    RUN_TEST(red_kb_tab_has_list);
    RUN_TEST(red_kb_tab_has_add_button);
    RUN_TEST(red_kb_tab_has_remove_button);
    RUN_TEST(red_kb_tab_has_edit_button);
    RUN_TEST(red_system_menu_settings_action);
    RUN_TEST(red_context_settings_action);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_keybinding_editor);
    GREATEST_MAIN_END();
}
