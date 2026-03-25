/* test_red_lua_config.c — RED tests for Lua config overrides
 *
 * Users can write Lua scripts to customize config beyond TOML.
 * The Lua engine runs overrides after TOML loading.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/config.h"
#include "wixen/config/lua_engine.h"

TEST red_lua_override_font_size(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT_EQ(14, (int)cfg.font.size);

    /* Write a temp Lua file that overrides font size */
    const char *lua_path = "test_override.lua";
    FILE *f = fopen(lua_path, "w");
    ASSERT(f != NULL);
    fprintf(f, "config.font.size = 18\n");
    fclose(f);

    ASSERT(wixen_config_apply_lua_overrides(&cfg, lua_path));
    ASSERT_EQ(18, (int)cfg.font.size);

    wixen_config_free(&cfg);
    remove(lua_path);
    PASS();
}

TEST red_lua_override_color_scheme(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);

    const char *lua_path = "test_colors.lua";
    FILE *f = fopen(lua_path, "w");
    ASSERT(f != NULL);
    fprintf(f, "config.window.theme = \"dracula\"\n");
    fclose(f);

    ASSERT(wixen_config_apply_lua_overrides(&cfg, lua_path));
    ASSERT_STR_EQ("dracula", cfg.window.theme);

    wixen_config_free(&cfg);
    remove(lua_path);
    PASS();
}

TEST red_lua_invalid_script(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);

    const char *lua_path = "test_bad.lua";
    FILE *f = fopen(lua_path, "w");
    ASSERT(f != NULL);
    fprintf(f, "this is not valid lua @#$%%\n");
    fclose(f);

    /* Should return false but not crash */
    ASSERT_FALSE(wixen_config_apply_lua_overrides(&cfg, lua_path));

    wixen_config_free(&cfg);
    remove(lua_path);
    PASS();
}

TEST red_lua_nonexistent_file(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Should return false gracefully */
    ASSERT_FALSE(wixen_config_apply_lua_overrides(&cfg, "does_not_exist.lua"));
    wixen_config_free(&cfg);
    PASS();
}

TEST red_lua_override_keybinding(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);

    const char *lua_path = "test_keys.lua";
    FILE *f = fopen(lua_path, "w");
    ASSERT(f != NULL);
    fprintf(f, "config.keybindings[\"ctrl+shift+x\"] = \"close_pane\"\n");
    fclose(f);

    ASSERT(wixen_config_apply_lua_overrides(&cfg, lua_path));
    const char *action = wixen_keybindings_lookup(&cfg.keybindings, "ctrl+shift+x");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("close_pane", action);

    wixen_config_free(&cfg);
    remove(lua_path);
    PASS();
}

SUITE(red_lua_config) {
    RUN_TEST(red_lua_override_font_size);
    RUN_TEST(red_lua_override_color_scheme);
    RUN_TEST(red_lua_invalid_script);
    RUN_TEST(red_lua_nonexistent_file);
    RUN_TEST(red_lua_override_keybinding);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_lua_config);
    GREATEST_MAIN_END();
}
