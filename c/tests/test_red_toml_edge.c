/* test_red_toml_edge.c — RED tests for TOML parser edge cases
 *
 * Our minimal TOML parser must handle real-world config files
 * including quoted strings, escape chars, comments, inline tables.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/config.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

static const char *TOML_PATH = "test_toml_edge.toml";

static void write_toml(const char *content) {
    FILE *f = fopen(TOML_PATH, "w");
    if (f) { fputs(content, f); fclose(f); }
}

TEST red_toml_quoted_string_with_spaces(void) {
    write_toml(
        "[font]\n"
        "family = \"Cascadia Code PL\"\n"
        "size = 14\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT_STR_EQ("Cascadia Code PL", cfg.font.family);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_comments(void) {
    write_toml(
        "# This is a comment\n"
        "[font]\n"
        "size = 16 # inline comment\n"
        "# family = \"Ignored\"\n"
        "ligatures = true\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT_EQ(16, (int)cfg.font.size);
    ASSERT(cfg.font.ligatures);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_boolean_values(void) {
    write_toml(
        "[font]\nligatures = false\n"
        "[terminal]\ncursor_blink = true\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT_FALSE(cfg.font.ligatures);
    ASSERT(cfg.terminal.cursor_blink);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_float_values(void) {
    write_toml(
        "[font]\nsize = 13.5\nline_height = 1.25\n"
        "[window]\nopacity = 0.92\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT(cfg.font.size > 13.0f && cfg.font.size < 14.0f);
    ASSERT(cfg.font.line_height > 1.2f && cfg.font.line_height < 1.3f);
    ASSERT(cfg.window.opacity > 0.9f && cfg.window.opacity < 0.95f);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_empty_sections(void) {
    write_toml(
        "[font]\n"
        "[terminal]\n"
        "[window]\ntheme = \"solarized\"\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT_STR_EQ("solarized", cfg.window.theme);
    /* Font should keep defaults */
    ASSERT(cfg.font.size > 0);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_windows_path(void) {
    write_toml(
        "[terminal]\ncursor_style = \"bar\"\nbell_style = \"mute\"\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT_STR_EQ("bar", cfg.terminal.cursor_style);
    ASSERT_STR_EQ("mute", cfg.terminal.bell_style);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_empty_file(void) {
    write_toml("");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Empty file should succeed — all defaults preserved */
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT(cfg.font.size > 0);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

TEST red_toml_unknown_keys_ignored(void) {
    write_toml(
        "[font]\nsize = 20\nunknown_key = 42\n"
        "[nonexistent_section]\nfoo = \"bar\"\n");
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Should load successfully, ignoring unknown keys */
    ASSERT(wixen_config_load(&cfg, TOML_PATH));
    ASSERT_EQ(20, (int)cfg.font.size);
    wixen_config_free(&cfg);
    remove(TOML_PATH);
    PASS();
}

SUITE(red_toml_edge) {
    RUN_TEST(red_toml_quoted_string_with_spaces);
    RUN_TEST(red_toml_comments);
    RUN_TEST(red_toml_boolean_values);
    RUN_TEST(red_toml_float_values);
    RUN_TEST(red_toml_empty_sections);
    RUN_TEST(red_toml_windows_path);
    RUN_TEST(red_toml_empty_file);
    RUN_TEST(red_toml_unknown_keys_ignored);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_toml_edge);
    GREATEST_MAIN_END();
}
