/* test_keybindings.c — Tests for keybinding normalization, parsing, lookup */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/config/keybindings.h"

/* === Normalization === */

TEST normalize_simple(void) {
    char *n = wixen_chord_normalize("ctrl+shift+t");
    ASSERT_STR_EQ("ctrl+shift+t", n);
    free(n);
    PASS();
}

TEST normalize_uppercase(void) {
    char *n = wixen_chord_normalize("Ctrl+Shift+T");
    ASSERT_STR_EQ("ctrl+shift+t", n);
    free(n);
    PASS();
}

TEST normalize_reorders_modifiers(void) {
    char *n = wixen_chord_normalize("shift+ctrl+t");
    ASSERT_STR_EQ("ctrl+shift+t", n);
    free(n);
    PASS();
}

TEST normalize_alt_names(void) {
    char *n = wixen_chord_normalize("Control+Meta+A");
    ASSERT_STR_EQ("ctrl+win+a", n);
    free(n);
    PASS();
}

TEST normalize_single_key(void) {
    char *n = wixen_chord_normalize("f11");
    ASSERT_STR_EQ("f11", n);
    free(n);
    PASS();
}

TEST normalize_empty(void) {
    char *n = wixen_chord_normalize("");
    ASSERT_STR_EQ("", n);
    free(n);
    PASS();
}

TEST normalize_null(void) {
    char *n = wixen_chord_normalize(NULL);
    ASSERT_STR_EQ("", n);
    free(n);
    PASS();
}

TEST normalize_all_modifiers(void) {
    char *n = wixen_chord_normalize("win+alt+shift+ctrl+x");
    ASSERT_STR_EQ("ctrl+shift+alt+win+x", n);
    free(n);
    PASS();
}

/* === Parsing === */

TEST parse_ctrl_shift_t(void) {
    WixenParsedChord p;
    bool ok = wixen_chord_parse("ctrl+shift+t", &p);
    ASSERT(ok);
    ASSERT(p.ctrl);
    ASSERT(p.shift);
    ASSERT_FALSE(p.alt);
    ASSERT_FALSE(p.win);
    ASSERT_STR_EQ("t", p.key);
    PASS();
}

TEST parse_f11(void) {
    WixenParsedChord p;
    bool ok = wixen_chord_parse("f11", &p);
    ASSERT(ok);
    ASSERT_FALSE(p.ctrl);
    ASSERT_FALSE(p.shift);
    ASSERT_STR_EQ("f11", p.key);
    PASS();
}

TEST parse_alt_enter(void) {
    WixenParsedChord p;
    bool ok = wixen_chord_parse("Alt+Enter", &p);
    ASSERT(ok);
    ASSERT(p.alt);
    ASSERT_STR_EQ("enter", p.key);
    PASS();
}

TEST parse_empty_fails(void) {
    WixenParsedChord p;
    bool ok = wixen_chord_parse("", &p);
    ASSERT_FALSE(ok);
    PASS();
}

/* === Map lookup === */

TEST map_add_and_lookup(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "ctrl+shift+t", "new_tab", NULL);
    const char *action = wixen_keybindings_lookup(&km, "ctrl+shift+t");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("new_tab", action);
    wixen_keybindings_free(&km);
    PASS();
}

TEST map_lookup_normalized(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "Shift+Ctrl+T", "new_tab", NULL);
    /* Lookup with different casing/order should still match */
    const char *action = wixen_keybindings_lookup(&km, "ctrl+shift+t");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("new_tab", action);
    wixen_keybindings_free(&km);
    PASS();
}

TEST map_lookup_not_found(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "ctrl+shift+t", "new_tab", NULL);
    ASSERT(wixen_keybindings_lookup(&km, "ctrl+shift+w") == NULL);
    wixen_keybindings_free(&km);
    PASS();
}

TEST map_defaults_loaded(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_load_defaults(&km);
    ASSERT(km.count > 30);
    ASSERT_STR_EQ("new_tab", wixen_keybindings_lookup(&km, "ctrl+shift+t"));
    ASSERT_STR_EQ("copy", wixen_keybindings_lookup(&km, "ctrl+shift+c"));
    ASSERT_STR_EQ("paste", wixen_keybindings_lookup(&km, "ctrl+shift+v"));
    ASSERT_STR_EQ("find", wixen_keybindings_lookup(&km, "ctrl+shift+f"));
    ASSERT_STR_EQ("command_palette", wixen_keybindings_lookup(&km, "ctrl+shift+p"));
    ASSERT_STR_EQ("settings", wixen_keybindings_lookup(&km, "ctrl+comma"));
    ASSERT_STR_EQ("toggle_fullscreen", wixen_keybindings_lookup(&km, "f11"));
    wixen_keybindings_free(&km);
    PASS();
}

TEST map_with_args(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "ctrl+shift+1", "send_text", "hello\n");
    ASSERT_STR_EQ("send_text", wixen_keybindings_lookup(&km, "ctrl+shift+1"));
    /* Check args are stored */
    ASSERT_STR_EQ("hello\n", km.bindings[0].args);
    wixen_keybindings_free(&km);
    PASS();
}

TEST map_override_binding(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "ctrl+t", "action1", NULL);
    wixen_keybindings_add(&km, "ctrl+t", "action2", NULL);
    /* First match wins (like the Rust version) */
    ASSERT_STR_EQ("action1", wixen_keybindings_lookup(&km, "ctrl+t"));
    wixen_keybindings_free(&km);
    PASS();
}

SUITE(normalization) {
    RUN_TEST(normalize_simple);
    RUN_TEST(normalize_uppercase);
    RUN_TEST(normalize_reorders_modifiers);
    RUN_TEST(normalize_alt_names);
    RUN_TEST(normalize_single_key);
    RUN_TEST(normalize_empty);
    RUN_TEST(normalize_null);
    RUN_TEST(normalize_all_modifiers);
}

SUITE(parsing) {
    RUN_TEST(parse_ctrl_shift_t);
    RUN_TEST(parse_f11);
    RUN_TEST(parse_alt_enter);
    RUN_TEST(parse_empty_fails);
}

SUITE(map_tests) {
    RUN_TEST(map_add_and_lookup);
    RUN_TEST(map_lookup_normalized);
    RUN_TEST(map_lookup_not_found);
    RUN_TEST(map_defaults_loaded);
    RUN_TEST(map_with_args);
    RUN_TEST(map_override_binding);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(normalization);
    RUN_SUITE(parsing);
    RUN_SUITE(map_tests);
    GREATEST_MAIN_END();
}
