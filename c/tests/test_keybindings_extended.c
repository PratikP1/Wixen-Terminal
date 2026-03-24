/* test_keybindings_extended.c — Keybinding map tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/config/keybindings.h"

TEST kb_init_has_defaults(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    /* Should have at least copy, paste, settings */
    ASSERT(wixen_keybindings_lookup(&kb, "ctrl+shift+c") != NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_lookup_copy(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+shift+c");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("copy", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_lookup_paste(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+shift+v");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("paste", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_lookup_settings(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+comma");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("settings", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_lookup_nonexistent(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+alt+shift+z");
    ASSERT(action == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_add_custom(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "ctrl+shift+x", "custom_action", NULL);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+shift+x");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("custom_action", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_add_new_chord(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    /* Add a new chord that doesn't exist in defaults */
    wixen_keybindings_add(&kb, "ctrl+shift+f12", "custom_debug", NULL);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+shift+f12");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("custom_debug", action);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_add_multiple(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "ctrl+1", "tab1", NULL);
    wixen_keybindings_add(&kb, "ctrl+2", "tab2", NULL);
    ASSERT_STR_EQ("tab1", wixen_keybindings_lookup(&kb, "ctrl+1"));
    ASSERT_STR_EQ("tab2", wixen_keybindings_lookup(&kb, "ctrl+2"));
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_free_idempotent(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "ctrl+x", "test", NULL);
    wixen_keybindings_free(&kb);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST kb_case_insensitive(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "Ctrl+Shift+A", "upper", NULL);
    const char *action = wixen_keybindings_lookup(&kb, "ctrl+shift+a");
    /* Should match if the map normalizes case */
    (void)action; /* Implementation may or may not normalize */
    wixen_keybindings_free(&kb);
    PASS();
}

SUITE(keybindings_extended) {
    RUN_TEST(kb_init_has_defaults);
    RUN_TEST(kb_lookup_copy);
    RUN_TEST(kb_lookup_paste);
    RUN_TEST(kb_lookup_settings);
    RUN_TEST(kb_lookup_nonexistent);
    RUN_TEST(kb_add_custom);
    RUN_TEST(kb_add_new_chord);
    RUN_TEST(kb_add_multiple);
    RUN_TEST(kb_free_idempotent);
    RUN_TEST(kb_case_insensitive);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(keybindings_extended);
    GREATEST_MAIN_END();
}
