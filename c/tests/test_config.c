/* test_config.c — Tests for TOML config loading */
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/config/config.h"

/* Write a temp config file for testing */
static const char *write_temp_config(const char *content) {
    static char path[256];
    snprintf(path, sizeof(path), "test_config_tmp.toml");
    FILE *f = fopen(path, "w");
    if (!f) return NULL;
    fputs(content, f);
    fclose(f);
    return path;
}

static void cleanup_temp(const char *path) {
    if (path) remove(path);
}

TEST config_defaults(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    ASSERT_STR_EQ("Cascadia Mono", cfg.font.family);
    ASSERT(cfg.font.size > 10.0f && cfg.font.size < 20.0f);
    ASSERT(cfg.font.ligatures);
    ASSERT_EQ(1200, (int)cfg.window.width);
    ASSERT_STR_EQ("block", cfg.terminal.cursor_style);
    ASSERT(cfg.terminal.cursor_blink);
    ASSERT_STR_EQ("all", cfg.accessibility.verbosity);
    ASSERT_EQ(100, (int)cfg.accessibility.output_debounce_ms);
    ASSERT(cfg.keybindings.count > 30);
    wixen_config_free(&cfg);
    PASS();
}

TEST config_load_font(void) {
    const char *path = write_temp_config(
        "[font]\n"
        "family = \"Consolas\"\n"
        "size = 12.0\n"
        "ligatures = false\n"
    );
    ASSERT(path != NULL);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    bool ok = wixen_config_load(&cfg, path);
    ASSERT(ok);
    ASSERT_STR_EQ("Consolas", cfg.font.family);
    ASSERT(cfg.font.size > 11.9f && cfg.font.size < 12.1f);
    ASSERT_FALSE(cfg.font.ligatures);
    wixen_config_free(&cfg);
    cleanup_temp(path);
    PASS();
}

TEST config_load_profiles(void) {
    const char *path = write_temp_config(
        "[[profiles]]\n"
        "name = \"PowerShell\"\n"
        "program = \"pwsh.exe\"\n"
        "is_default = true\n"
        "\n"
        "[[profiles]]\n"
        "name = \"CMD\"\n"
        "program = \"cmd.exe\"\n"
    );
    ASSERT(path != NULL);
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    bool ok = wixen_config_load(&cfg, path);
    ASSERT(ok);
    ASSERT_EQ(2, (int)cfg.profile_count);
    ASSERT_STR_EQ("PowerShell", cfg.profiles[0].name);
    ASSERT_STR_EQ("pwsh.exe", cfg.profiles[0].program);
    ASSERT(cfg.profiles[0].is_default);
    ASSERT_STR_EQ("CMD", cfg.profiles[1].name);
    ASSERT_FALSE(cfg.profiles[1].is_default);
    wixen_config_free(&cfg);
    cleanup_temp(path);
    PASS();
}

TEST config_default_profile(void) {
    const char *path = write_temp_config(
        "[[profiles]]\n"
        "name = \"First\"\n"
        "program = \"first.exe\"\n"
        "\n"
        "[[profiles]]\n"
        "name = \"Second\"\n"
        "program = \"second.exe\"\n"
        "is_default = true\n"
    );
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, path);
    const WixenProfile *def = wixen_config_default_profile(&cfg);
    ASSERT(def != NULL);
    ASSERT_STR_EQ("Second", def->name);
    wixen_config_free(&cfg);
    cleanup_temp(path);
    PASS();
}

TEST config_default_profile_fallback_first(void) {
    const char *path = write_temp_config(
        "[[profiles]]\n"
        "name = \"Only\"\n"
        "program = \"only.exe\"\n"
    );
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, path);
    const WixenProfile *def = wixen_config_default_profile(&cfg);
    ASSERT(def != NULL);
    ASSERT_STR_EQ("Only", def->name);
    wixen_config_free(&cfg);
    cleanup_temp(path);
    PASS();
}

TEST config_load_accessibility(void) {
    const char *path = write_temp_config(
        "[accessibility]\n"
        "verbosity = \"basic\"\n"
        "output_debounce_ms = 200\n"
    );
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    wixen_config_load(&cfg, path);
    ASSERT_STR_EQ("basic", cfg.accessibility.verbosity);
    ASSERT_EQ(200, (int)cfg.accessibility.output_debounce_ms);
    wixen_config_free(&cfg);
    cleanup_temp(path);
    PASS();
}

TEST config_save_and_reload(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    free(cfg.font.family);
    cfg.font.family = _strdup("Hack");
    cfg.font.size = 16.0f;

    const char *path = "test_config_save.toml";
    bool ok = wixen_config_save(&cfg, path);
    ASSERT(ok);
    wixen_config_free(&cfg);

    /* Reload */
    memset(&cfg, 0, sizeof(cfg));
    ok = wixen_config_load(&cfg, path);
    ASSERT(ok);
    ASSERT_STR_EQ("Hack", cfg.font.family);
    ASSERT(cfg.font.size > 15.9f && cfg.font.size < 16.1f);
    wixen_config_free(&cfg);
    remove(path);
    PASS();
}

TEST config_load_nonexistent_fails(void) {
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_FALSE(wixen_config_load(&cfg, "nonexistent_file.toml"));
    PASS();
}

TEST config_no_profiles_empty(void) {
    WixenConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    const WixenProfile *def = wixen_config_default_profile(&cfg);
    ASSERT(def == NULL);
    PASS();
}

SUITE(config_defaults_suite) {
    RUN_TEST(config_defaults);
}

SUITE(config_loading) {
    RUN_TEST(config_load_font);
    RUN_TEST(config_load_profiles);
    RUN_TEST(config_load_accessibility);
    RUN_TEST(config_load_nonexistent_fails);
}

SUITE(config_profiles) {
    RUN_TEST(config_default_profile);
    RUN_TEST(config_default_profile_fallback_first);
    RUN_TEST(config_no_profiles_empty);
}

SUITE(config_save_suite) {
    RUN_TEST(config_save_and_reload);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(config_defaults_suite);
    RUN_SUITE(config_loading);
    RUN_SUITE(config_profiles);
    RUN_SUITE(config_save_suite);
    GREATEST_MAIN_END();
}
