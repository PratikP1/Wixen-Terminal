/* test_red_config_roundtrip.c — RED tests for config save/load round-trip */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/config.h"

TEST red_config_roundtrip_font(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Change font */
    free(cfg.font.family);
    cfg.font.family = _strdup("JetBrains Mono");
    cfg.font.size = 16.0f;

    const char *path = "test_config_rt.toml";
    bool saved = wixen_config_save(&cfg, path);
    ASSERT(saved);

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    bool ok = wixen_config_load(&loaded, path);
    ASSERT(ok);
    ASSERT_STR_EQ("JetBrains Mono", loaded.font.family);
    ASSERT(loaded.font.size > 15.0f && loaded.font.size < 17.0f);

    wixen_config_free(&loaded);
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

TEST red_config_roundtrip_window(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    cfg.window.opacity = 0.85f;

    const char *path = "test_config_rt2.toml";
    wixen_config_save(&cfg, path);

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    wixen_config_load(&loaded, path);
    ASSERT(loaded.window.opacity > 0.84f && loaded.window.opacity < 0.86f);

    wixen_config_free(&loaded);
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

TEST red_config_roundtrip_terminal(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    free(cfg.terminal.cursor_style);
    cfg.terminal.cursor_style = _strdup("bar");
    cfg.terminal.cursor_blink = false;

    const char *path = "test_config_rt3.toml";
    wixen_config_save(&cfg, path);

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    wixen_config_load(&loaded, path);
    ASSERT_STR_EQ("bar", loaded.terminal.cursor_style);
    ASSERT_FALSE(loaded.terminal.cursor_blink);

    wixen_config_free(&loaded);
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

TEST red_config_roundtrip_a11y(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    free(cfg.accessibility.verbosity);
    cfg.accessibility.verbosity = _strdup("basic");
    cfg.accessibility.output_debounce_ms = 200;

    const char *path = "test_config_rt4.toml";
    wixen_config_save(&cfg, path);

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    wixen_config_load(&loaded, path);
    ASSERT_STR_EQ("basic", loaded.accessibility.verbosity);
    ASSERT_EQ(200, (int)loaded.accessibility.output_debounce_ms);

    wixen_config_free(&loaded);
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

TEST red_config_load_malformed(void) {
    const char *path = "test_config_bad.toml";
    FILE *f = fopen(path, "w");
    if (!f) SKIP();
    fprintf(f, "[[[invalid toml\nnot = valid\n");
    fclose(f);

    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Load should handle gracefully — either fail or partial load */
    wixen_config_load(&cfg, path);
    /* Defaults should still be intact for unparsed fields */
    ASSERT(cfg.font.family != NULL);
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

TEST red_config_empty_file(void) {
    const char *path = "test_config_empty.toml";
    FILE *f = fopen(path, "w");
    if (!f) SKIP();
    fclose(f);

    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    bool ok = wixen_config_load(&cfg, path);
    /* Empty file = valid TOML, just no overrides */
    ASSERT(ok);
    ASSERT(cfg.font.family != NULL); /* Defaults intact */
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

SUITE(red_config_roundtrip) {
    RUN_TEST(red_config_roundtrip_font);
    RUN_TEST(red_config_roundtrip_window);
    RUN_TEST(red_config_roundtrip_terminal);
    RUN_TEST(red_config_roundtrip_a11y);
    RUN_TEST(red_config_load_malformed);
    RUN_TEST(red_config_empty_file);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_config_roundtrip);
    GREATEST_MAIN_END();
}
