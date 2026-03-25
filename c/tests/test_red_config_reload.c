/* test_red_config_reload.c — RED tests for config hot-reload delta
 *
 * When config file changes on disk, detect which fields changed
 * and apply only the delta (don't restart everything).
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define strdup _strdup
#endif
#include "greatest.h"
#include "wixen/config/config.h"

TEST red_config_diff_font_changed(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);
    new_cfg.font.size = 18.0f;

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT(delta.font_changed);
    ASSERT_FALSE(delta.colors_changed);
    ASSERT_FALSE(delta.keybindings_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

TEST red_config_diff_colors_changed(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);
    free(new_cfg.window.theme);
    new_cfg.window.theme = strdup("dracula");

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT(delta.colors_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

TEST red_config_diff_nothing_changed(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT_FALSE(delta.font_changed);
    ASSERT_FALSE(delta.colors_changed);
    ASSERT_FALSE(delta.keybindings_changed);
    ASSERT_FALSE(delta.terminal_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

TEST red_config_diff_terminal_changed(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);
    new_cfg.terminal.cursor_blink = !old_cfg.terminal.cursor_blink;

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT(delta.terminal_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

SUITE(red_config_reload) {
    RUN_TEST(red_config_diff_font_changed);
    RUN_TEST(red_config_diff_colors_changed);
    RUN_TEST(red_config_diff_nothing_changed);
    RUN_TEST(red_config_diff_terminal_changed);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_config_reload);
    GREATEST_MAIN_END();
}
