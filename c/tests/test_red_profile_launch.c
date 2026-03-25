/* test_red_profile_launch.c — RED tests for profile-based shell launch
 *
 * The terminal should launch the shell from the default config profile,
 * not from hardcoded shell detection. If no profile is configured,
 * fall back to detect_shell.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/config/config.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

TEST red_default_profile_exists(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *p = wixen_config_default_profile(&cfg);
    ASSERT(p != NULL);
    ASSERT(p->program != NULL);
    ASSERT(strlen(p->program) > 0);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_default_profile_is_powershell(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *p = wixen_config_default_profile(&cfg);
    ASSERT(p != NULL);
    /* Default profile should be PowerShell */
    ASSERT(strstr(p->program, "pwsh") != NULL ||
           strstr(p->program, "powershell") != NULL ||
           strstr(p->program, "PowerShell") != NULL);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_profile_has_name(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *p = wixen_config_default_profile(&cfg);
    ASSERT(p != NULL);
    ASSERT(p->name != NULL);
    ASSERT(strlen(p->name) > 0);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_profile_shell_to_wide(void) {
    /* The profile program must be convertible to wchar_t for CreateProcessW */
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *p = wixen_config_default_profile(&cfg);
    ASSERT(p != NULL);
    /* Simulate the conversion */
    size_t len = strlen(p->program);
    ASSERT(len > 0);
    ASSERT(len < 260); /* MAX_PATH */
    wixen_config_free(&cfg);
    PASS();
}

TEST red_multiple_profiles(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Defaults should include at least PowerShell */
    ASSERT(cfg.profile_count >= 1);
    for (size_t i = 0; i < cfg.profile_count; i++) {
        ASSERT(cfg.profiles[i].name != NULL);
        ASSERT(cfg.profiles[i].program != NULL);
    }
    wixen_config_free(&cfg);
    PASS();
}

SUITE(red_profile_launch) {
    RUN_TEST(red_default_profile_exists);
    RUN_TEST(red_default_profile_is_powershell);
    RUN_TEST(red_profile_has_name);
    RUN_TEST(red_profile_shell_to_wide);
    RUN_TEST(red_multiple_profiles);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_profile_launch);
    GREATEST_MAIN_END();
}
