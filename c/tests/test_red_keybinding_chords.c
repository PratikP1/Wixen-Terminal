/* test_red_keybinding_chords.c — RED tests for chord normalization + lookup */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/config/keybindings.h"

TEST red_chord_case_insensitive(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "ctrl+shift+a", "test_action", NULL);
    /* Lookup with different case should still match */
    const char *a = wixen_keybindings_lookup(&kb, "Ctrl+Shift+A");
    /* If normalization works, this matches. If not, NULL. */
    /* This is likely RED — most impls are case-sensitive */
    ASSERT(a != NULL);
    ASSERT_STR_EQ("test_action", a);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_order_independent(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_add(&kb, "ctrl+shift+a", "action1", NULL);
    /* Lookup with reversed modifier order */
    const char *a = wixen_keybindings_lookup(&kb, "shift+ctrl+a");
    /* If order normalization works, this matches */
    ASSERT(a != NULL);
    ASSERT_STR_EQ("action1", a);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_default_palette(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *a = wixen_keybindings_lookup(&kb, "ctrl+shift+p");
    ASSERT(a != NULL);
    ASSERT_STR_EQ("command_palette", a);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_default_new_tab(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *a = wixen_keybindings_lookup(&kb, "ctrl+shift+t");
    ASSERT(a != NULL);
    ASSERT_STR_EQ("new_tab", a);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_default_close_pane(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *a = wixen_keybindings_lookup(&kb, "ctrl+shift+w");
    ASSERT(a != NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_default_fullscreen(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *a = wixen_keybindings_lookup(&kb, "f11");
    ASSERT(a != NULL);
    ASSERT_STR_EQ("toggle_fullscreen", a);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_default_search(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *a = wixen_keybindings_lookup(&kb, "ctrl+shift+f");
    ASSERT(a != NULL);
    ASSERT_STR_EQ("find", a);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_empty_string(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    wixen_keybindings_load_defaults(&kb);
    const char *a = wixen_keybindings_lookup(&kb, "");
    ASSERT(a == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

TEST red_chord_null_string(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    const char *a = wixen_keybindings_lookup(&kb, NULL);
    ASSERT(a == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

SUITE(red_keybinding_chords) {
    RUN_TEST(red_chord_case_insensitive);
    RUN_TEST(red_chord_order_independent);
    RUN_TEST(red_chord_default_palette);
    RUN_TEST(red_chord_default_new_tab);
    RUN_TEST(red_chord_default_close_pane);
    RUN_TEST(red_chord_default_fullscreen);
    RUN_TEST(red_chord_default_search);
    RUN_TEST(red_chord_empty_string);
    RUN_TEST(red_chord_null_string);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_keybinding_chords);
    GREATEST_MAIN_END();
}
