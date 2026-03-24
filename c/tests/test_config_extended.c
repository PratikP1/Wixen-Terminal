/* test_config_extended.c — Extended config tests: keybinding chord edge cases,
 * profile management, validation */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/config/keybindings.h"
#include "wixen/config/config.h"

/* === Keybinding edge cases === */

TEST kb_whitespace_in_chord(void) {
    char *n = wixen_chord_normalize(" ctrl + shift + t ");
    ASSERT_STR_EQ("ctrl+shift+t", n);
    free(n);
    PASS();
}

TEST kb_duplicate_modifiers(void) {
    char *n = wixen_chord_normalize("ctrl+ctrl+t");
    /* Should not have double ctrl */
    ASSERT_STR_EQ("ctrl+t", n);
    free(n);
    PASS();
}

TEST kb_cmd_maps_to_win(void) {
    char *n = wixen_chord_normalize("cmd+a");
    ASSERT_STR_EQ("win+a", n);
    free(n);
    PASS();
}

TEST kb_super_maps_to_win(void) {
    char *n = wixen_chord_normalize("super+a");
    ASSERT_STR_EQ("win+a", n);
    free(n);
    PASS();
}

TEST kb_lookup_case_insensitive(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "Ctrl+Shift+T", "new_tab", NULL);
    ASSERT_STR_EQ("new_tab", wixen_keybindings_lookup(&km, "CTRL+SHIFT+T"));
    ASSERT_STR_EQ("new_tab", wixen_keybindings_lookup(&km, "ctrl+shift+t"));
    wixen_keybindings_free(&km);
    PASS();
}

TEST kb_empty_map_lookup(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    ASSERT(wixen_keybindings_lookup(&km, "ctrl+x") == NULL);
    wixen_keybindings_free(&km);
    PASS();
}

/* === Config edge cases === */

TEST config_window_opacity(void) {
    const char *toml =
        "[window]\n"
        "opacity = 0.85\n";
    FILE *f = fopen("test_opacity.toml", "w");
    fputs(toml, f);
    fclose(f);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, "test_opacity.toml");
    ASSERT(cfg.window.opacity > 0.84f && cfg.window.opacity < 0.86f);
    wixen_config_free(&cfg);
    remove("test_opacity.toml");
    PASS();
}

TEST config_terminal_settings(void) {
    const char *toml =
        "[terminal]\n"
        "cursor_style = \"bar\"\n"
        "cursor_blink = false\n"
        "bell_style = \"mute\"\n";
    FILE *f = fopen("test_term.toml", "w");
    fputs(toml, f);
    fclose(f);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, "test_term.toml");
    ASSERT_STR_EQ("bar", cfg.terminal.cursor_style);
    ASSERT_FALSE(cfg.terminal.cursor_blink);
    ASSERT_STR_EQ("mute", cfg.terminal.bell_style);
    wixen_config_free(&cfg);
    remove("test_term.toml");
    PASS();
}

TEST config_behavior_settings(void) {
    const char *toml =
        "[behavior]\n"
        "copy_on_selection = true\n"
        "paste_with_ctrl_v = false\n";
    FILE *f = fopen("test_behav.toml", "w");
    fputs(toml, f);
    fclose(f);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, "test_behav.toml");
    ASSERT(cfg.behavior.copy_on_selection);
    ASSERT_FALSE(cfg.behavior.paste_with_ctrl_v);
    wixen_config_free(&cfg);
    remove("test_behav.toml");
    PASS();
}

TEST config_empty_file(void) {
    FILE *f = fopen("test_empty.toml", "w");
    fputs("# Empty config\n", f);
    fclose(f);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    bool ok = wixen_config_load(&cfg, "test_empty.toml");
    ASSERT(ok);
    /* Defaults should be populated */
    ASSERT(cfg.font.family != NULL);
    wixen_config_free(&cfg);
    remove("test_empty.toml");
    PASS();
}

TEST config_multiple_profiles(void) {
    const char *toml =
        "[[profiles]]\n"
        "name = \"A\"\n"
        "program = \"a.exe\"\n"
        "\n"
        "[[profiles]]\n"
        "name = \"B\"\n"
        "program = \"b.exe\"\n"
        "\n"
        "[[profiles]]\n"
        "name = \"C\"\n"
        "program = \"c.exe\"\n"
        "is_default = true\n";
    FILE *f = fopen("test_multi_prof.toml", "w");
    fputs(toml, f);
    fclose(f);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, "test_multi_prof.toml");
    ASSERT_EQ(3, (int)cfg.profile_count);
    const WixenProfile *def = wixen_config_default_profile(&cfg);
    ASSERT_STR_EQ("C", def->name);
    wixen_config_free(&cfg);
    remove("test_multi_prof.toml");
    PASS();
}

SUITE(kb_edge_cases) {
    RUN_TEST(kb_whitespace_in_chord);
    RUN_TEST(kb_duplicate_modifiers);
    RUN_TEST(kb_cmd_maps_to_win);
    RUN_TEST(kb_super_maps_to_win);
    RUN_TEST(kb_lookup_case_insensitive);
    RUN_TEST(kb_empty_map_lookup);
}

SUITE(config_extended) {
    RUN_TEST(config_window_opacity);
    RUN_TEST(config_terminal_settings);
    RUN_TEST(config_behavior_settings);
    RUN_TEST(config_empty_file);
    RUN_TEST(config_multiple_profiles);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(kb_edge_cases);
    RUN_SUITE(config_extended);
    GREATEST_MAIN_END();
}
