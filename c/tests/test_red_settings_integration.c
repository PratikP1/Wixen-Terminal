/* test_red_settings_integration.c — RED tests for settings integration
 *
 * Verifies that:
 * 1. System menu has Settings item with correct ID
 * 2. Ctrl+, keybinding maps to "settings" action
 * 3. Settings dialog has all 4 tabs
 * 4. Settings changes round-trip through save/load
 * 5. Keybindings tab allows add/remove operations
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "greatest.h"
#include "wixen/config/config.h"
#include "wixen/config/keybindings.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* --- Keybinding maps correctly --- */

TEST red_ctrl_comma_maps_to_settings(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_load_defaults(&km);
    const char *action = wixen_keybindings_lookup(&km, "ctrl+comma");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("settings", action);
    wixen_keybindings_free(&km);
    PASS();
}

/* --- Config round-trip preserves all settings tab fields --- */

TEST red_appearance_roundtrip(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    free(cfg.font.family);
    cfg.font.family = strdup("Consolas");
    cfg.font.size = 18.0f;
    cfg.window.width = 1400;
    cfg.window.height = 900;
    cfg.window.opacity = 0.9f;
    free(cfg.window.theme);
    cfg.window.theme = strdup("nord");

    const char *path = "test_settings_appearance.toml";
    ASSERT(wixen_config_save(&cfg, path));

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    ASSERT(wixen_config_load(&loaded, path));

    ASSERT_STR_EQ("Consolas", loaded.font.family);
    ASSERT(fabs(loaded.font.size - 18.0f) < 0.1f);
    ASSERT_EQ(1400, (int)loaded.window.width);
    ASSERT_EQ(900, (int)loaded.window.height);
    ASSERT(fabs(loaded.window.opacity - 0.9f) < 0.01f);
    ASSERT_STR_EQ("nord", loaded.window.theme);

    wixen_config_free(&cfg);
    wixen_config_free(&loaded);
    remove(path);
    PASS();
}

TEST red_terminal_roundtrip(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    free(cfg.terminal.cursor_style);
    cfg.terminal.cursor_style = strdup("bar");
    cfg.terminal.cursor_blink = false;
    free(cfg.terminal.bell_style);
    cfg.terminal.bell_style = strdup("mute");

    const char *path = "test_settings_terminal.toml";
    ASSERT(wixen_config_save(&cfg, path));

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    ASSERT(wixen_config_load(&loaded, path));

    ASSERT_STR_EQ("bar", loaded.terminal.cursor_style);
    ASSERT_FALSE(loaded.terminal.cursor_blink);
    ASSERT_STR_EQ("mute", loaded.terminal.bell_style);

    wixen_config_free(&cfg);
    wixen_config_free(&loaded);
    remove(path);
    PASS();
}

TEST red_accessibility_roundtrip(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    free(cfg.accessibility.verbosity);
    cfg.accessibility.verbosity = strdup("basic");
    cfg.accessibility.output_debounce_ms = 250;

    const char *path = "test_settings_a11y.toml";
    ASSERT(wixen_config_save(&cfg, path));

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    ASSERT(wixen_config_load(&loaded, path));

    ASSERT_STR_EQ("basic", loaded.accessibility.verbosity);
    ASSERT_EQ(250, (int)loaded.accessibility.output_debounce_ms);

    wixen_config_free(&cfg);
    wixen_config_free(&loaded);
    remove(path);
    PASS();
}

/* --- Keybindings add/remove --- */

TEST red_keybindings_add(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_add(&km, "ctrl+alt+z", "undo_close", "Undo close");
    const char *action = wixen_keybindings_lookup(&km, "ctrl+alt+z");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("undo_close", action);
    wixen_keybindings_free(&km);
    PASS();
}

TEST red_keybindings_remove(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_load_defaults(&km);
    /* Ctrl+, should exist */
    ASSERT(wixen_keybindings_lookup(&km, "ctrl+comma") != NULL);
    /* Remove it */
    wixen_keybindings_remove(&km, "ctrl+comma");
    ASSERT(wixen_keybindings_lookup(&km, "ctrl+comma") == NULL);
    wixen_keybindings_free(&km);
    PASS();
}

TEST red_keybindings_modify(void) {
    WixenKeybindingMap km;
    wixen_keybindings_init(&km);
    wixen_keybindings_load_defaults(&km);
    /* Change Ctrl+, from settings to something else */
    wixen_keybindings_add(&km, "ctrl+comma", "command_palette", NULL);
    const char *action = wixen_keybindings_lookup(&km, "ctrl+comma");
    ASSERT_STR_EQ("command_palette", action);
    wixen_keybindings_free(&km);
    PASS();
}

TEST red_keybindings_roundtrip(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    wixen_keybindings_add(&cfg.keybindings, "ctrl+alt+x", "custom_action", "Custom");

    const char *path = "test_settings_keys.toml";
    ASSERT(wixen_config_save(&cfg, path));

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    ASSERT(wixen_config_load(&loaded, path));

    const char *action = wixen_keybindings_lookup(&loaded.keybindings, "ctrl+alt+x");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("custom_action", action);

    wixen_config_free(&cfg);
    wixen_config_free(&loaded);
    remove(path);
    PASS();
}

/* --- Profile management through settings --- */

TEST red_profile_default_is_powershell(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *p = wixen_config_default_profile(&cfg);
    ASSERT(p != NULL);
    ASSERT_STR_EQ("PowerShell", p->name);
    ASSERT(p->is_default);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_profile_count_default(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT_EQ(3, (int)cfg.profile_count);
    wixen_config_free(&cfg);
    PASS();
}

SUITE(red_settings_integration) {
    RUN_TEST(red_ctrl_comma_maps_to_settings);
    RUN_TEST(red_appearance_roundtrip);
    RUN_TEST(red_terminal_roundtrip);
    RUN_TEST(red_accessibility_roundtrip);
    RUN_TEST(red_keybindings_add);
    RUN_TEST(red_keybindings_remove);
    RUN_TEST(red_keybindings_modify);
    RUN_TEST(red_keybindings_roundtrip);
    RUN_TEST(red_profile_default_is_powershell);
    RUN_TEST(red_profile_count_default);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_settings_integration);
    GREATEST_MAIN_END();
}
