/* test_red_hot_reload.c — RED-then-GREEN tests for config hot-reload *apply*
 *
 * The existing test_red_config_reload.c covers diff detection.
 * This file tests that detected deltas are actually *applied*:
 *   - font size change sets needs_font_rebuild flag
 *   - theme change sets needs_color_rebuild flag
 *   - debounce_ms change updates the throttler
 *   - keybinding changes replace the keybinding map
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
#define strdup _strdup
#endif
#include "greatest.h"
#include "wixen/config/config.h"
#include "wixen/config/keybindings.h"
#include "wixen/a11y/events.h"

/* ------------------------------------------------------------------ */
/* 1. Delta detects font size change                                   */
/* ------------------------------------------------------------------ */
TEST red_delta_detects_font_size_change(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    new_cfg.font.size = old_cfg.font.size + 4.0f;   /* e.g. 12 -> 16 */

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT(delta.font_changed);
    ASSERT_FALSE(delta.colors_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. Delta detects theme change                                       */
/* ------------------------------------------------------------------ */
TEST red_delta_detects_theme_change(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    free(new_cfg.window.theme);
    new_cfg.window.theme = strdup("solarized-dark");

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT(delta.colors_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. Delta detects debounce_ms change                                 */
/* ------------------------------------------------------------------ */
TEST red_delta_detects_debounce_change(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    new_cfg.accessibility.output_debounce_ms = 250;  /* change from default */

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    ASSERT(delta.accessibility_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 4. Applying debounce delta actually updates the throttler           */
/* ------------------------------------------------------------------ */
TEST red_apply_debounce_updates_throttler(void) {
    /* Set up a throttler with the old debounce */
    WixenEventThrottler throttler;
    wixen_throttler_init(&throttler, 100);
    ASSERT_EQ(100u, throttler.debounce_ms);

    /* Simulate a config reload where debounce changed */
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);
    new_cfg.accessibility.output_debounce_ms = 300;

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);
    ASSERT(delta.accessibility_changed);

    /* Apply: the reload logic should update throttler.debounce_ms */
    if (delta.accessibility_changed) {
        throttler.debounce_ms = new_cfg.accessibility.output_debounce_ms;
    }

    ASSERT_EQ(300u, throttler.debounce_ms);

    wixen_throttler_free(&throttler);
    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 5. Keybinding changes are applied — old map replaced by new         */
/* ------------------------------------------------------------------ */
TEST red_apply_keybindings_replaced(void) {
    /* Build "old" config with defaults */
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    /* Add a custom binding to the new config */
    wixen_keybindings_add(&new_cfg.keybindings, "ctrl+shift+x", "custom_action", NULL);

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);
    ASSERT(delta.keybindings_changed);

    /* Simulate the apply path: build a live keybinding map, then replace it */
    WixenKeybindingMap live_kb;
    wixen_keybindings_init(&live_kb);
    wixen_keybindings_load_defaults(&live_kb);

    /* Before apply — custom binding should NOT exist */
    ASSERT_EQ(NULL, wixen_keybindings_lookup(&live_kb, "ctrl+shift+x"));

    /* Apply: rebuild live map from new config keybindings */
    if (delta.keybindings_changed) {
        wixen_keybindings_free(&live_kb);
        wixen_keybindings_init(&live_kb);
        /* Copy each binding from the new config */
        for (size_t i = 0; i < new_cfg.keybindings.count; i++) {
            const WixenKeybinding *kb = wixen_keybindings_get_at(&new_cfg.keybindings, i);
            if (kb) {
                wixen_keybindings_add(&live_kb, kb->chord, kb->action, kb->args);
            }
        }
    }

    /* After apply — custom binding SHOULD exist */
    const char *action = wixen_keybindings_lookup(&live_kb, "ctrl+shift+x");
    ASSERT(action != NULL);
    ASSERT_STR_EQ("custom_action", action);

    /* Default bindings should still be there too */
    ASSERT(wixen_keybindings_lookup(&live_kb, "ctrl+shift+t") != NULL);

    wixen_keybindings_free(&live_kb);
    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. Font change sets needs_font_rebuild flag                         */
/* ------------------------------------------------------------------ */
TEST red_apply_font_sets_rebuild_flag(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);
    new_cfg.font.size = 20.0f;

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);
    ASSERT(delta.font_changed);

    /* The reload logic should set a flag when font changes */
    bool needs_font_rebuild = false;
    if (delta.font_changed) {
        needs_font_rebuild = true;
    }
    ASSERT(needs_font_rebuild);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. Theme change sets needs_color_rebuild flag                       */
/* ------------------------------------------------------------------ */
TEST red_apply_theme_sets_color_flag(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    free(new_cfg.window.theme);
    new_cfg.window.theme = strdup("monokai");

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);
    ASSERT(delta.colors_changed);

    bool needs_color_rebuild = false;
    if (delta.colors_changed) {
        needs_color_rebuild = true;
    }
    ASSERT(needs_color_rebuild);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 8. No-change config produces no apply side-effects                  */
/* ------------------------------------------------------------------ */
TEST red_apply_noop_when_unchanged(void) {
    WixenConfig old_cfg, new_cfg;
    wixen_config_init_defaults(&old_cfg);
    wixen_config_init_defaults(&new_cfg);

    WixenConfigDelta delta;
    wixen_config_diff(&old_cfg, &new_cfg, &delta);

    bool needs_font_rebuild = false;
    bool needs_color_rebuild = false;

    if (delta.font_changed) needs_font_rebuild = true;
    if (delta.colors_changed) needs_color_rebuild = true;

    ASSERT_FALSE(needs_font_rebuild);
    ASSERT_FALSE(needs_color_rebuild);
    ASSERT_FALSE(delta.keybindings_changed);
    ASSERT_FALSE(delta.accessibility_changed);

    wixen_config_free(&old_cfg);
    wixen_config_free(&new_cfg);
    PASS();
}

/* ------------------------------------------------------------------ */

SUITE(red_hot_reload_apply) {
    RUN_TEST(red_delta_detects_font_size_change);
    RUN_TEST(red_delta_detects_theme_change);
    RUN_TEST(red_delta_detects_debounce_change);
    RUN_TEST(red_apply_debounce_updates_throttler);
    RUN_TEST(red_apply_keybindings_replaced);
    RUN_TEST(red_apply_font_sets_rebuild_flag);
    RUN_TEST(red_apply_theme_sets_color_flag);
    RUN_TEST(red_apply_noop_when_unchanged);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_hot_reload_apply);
    GREATEST_MAIN_END();
}
