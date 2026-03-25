/* test_red_profile_uuid.c — RED tests for stable profile UUIDs
 *
 * Each profile gets a UUID on creation. Session saves reference
 * profiles by UUID, not name or program path. Names can change,
 * programs can change, UUIDs are stable.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/config/config.h"
#include "wixen/config/session.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

TEST red_profile_has_uuid(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Every default profile should have a non-empty UUID */
    for (size_t i = 0; i < cfg.profile_count; i++) {
        ASSERT(cfg.profiles[i].uuid != NULL);
        ASSERT(strlen(cfg.profiles[i].uuid) > 0);
    }
    wixen_config_free(&cfg);
    PASS();
}

TEST red_profile_uuids_unique(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* All UUIDs should be different */
    for (size_t i = 0; i < cfg.profile_count; i++) {
        for (size_t j = i + 1; j < cfg.profile_count; j++) {
            ASSERT(strcmp(cfg.profiles[i].uuid, cfg.profiles[j].uuid) != 0);
        }
    }
    wixen_config_free(&cfg);
    PASS();
}

TEST red_profile_uuid_stable_across_loads(void) {
    /* Save config, reload — UUIDs should be identical */
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const char *path = "test_uuid_config.toml";
    ASSERT(wixen_config_save(&cfg, path));

    char *uuid0 = strdup(cfg.profiles[0].uuid);

    WixenConfig loaded;
    wixen_config_init_defaults(&loaded);
    ASSERT(wixen_config_load(&loaded, path));
    ASSERT_STR_EQ(uuid0, loaded.profiles[0].uuid);

    free(uuid0);
    wixen_config_free(&cfg);
    wixen_config_free(&loaded);
    remove(path);
    PASS();
}

TEST red_session_saves_uuid(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    wixen_session_add_tab(&ss, "Dev", "abc-123-def", "C:\\Projects");
    const char *path = "test_uuid_session.json";
    ASSERT(wixen_session_save(&ss, path));

    WixenSessionState loaded;
    wixen_session_init(&loaded);
    ASSERT(wixen_session_load(&loaded, path));
    ASSERT_STR_EQ("abc-123-def", loaded.tabs[0].profile_name);

    wixen_session_free(&ss);
    wixen_session_free(&loaded);
    remove(path);
    PASS();
}

TEST red_resolve_profile_by_uuid(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Look up profile by its UUID */
    const char *uuid = cfg.profiles[1].uuid; /* CMD */
    const WixenProfile *found = wixen_config_profile_by_uuid(&cfg, uuid);
    ASSERT(found != NULL);
    ASSERT_STR_EQ("Command Prompt", found->name);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_resolve_missing_uuid_returns_default(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *found = wixen_config_profile_by_uuid(&cfg, "nonexistent-uuid");
    /* Should fall back to default profile */
    ASSERT(found != NULL);
    ASSERT(found->is_default);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_new_profile_gets_uuid(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    size_t before = cfg.profile_count;
    wixen_config_add_profile(&cfg, "Git Bash", "bash.exe", false);
    ASSERT_EQ((int)before + 1, (int)cfg.profile_count);
    ASSERT(cfg.profiles[cfg.profile_count - 1].uuid != NULL);
    ASSERT(strlen(cfg.profiles[cfg.profile_count - 1].uuid) > 0);
    /* UUID should differ from all existing */
    for (size_t i = 0; i < cfg.profile_count - 1; i++) {
        ASSERT(strcmp(cfg.profiles[i].uuid, cfg.profiles[cfg.profile_count - 1].uuid) != 0);
    }
    wixen_config_free(&cfg);
    PASS();
}

SUITE(red_profile_uuid) {
    RUN_TEST(red_profile_has_uuid);
    RUN_TEST(red_profile_uuids_unique);
    RUN_TEST(red_profile_uuid_stable_across_loads);
    RUN_TEST(red_session_saves_uuid);
    RUN_TEST(red_resolve_profile_by_uuid);
    RUN_TEST(red_resolve_missing_uuid_returns_default);
    RUN_TEST(red_new_profile_gets_uuid);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_profile_uuid);
    GREATEST_MAIN_END();
}
