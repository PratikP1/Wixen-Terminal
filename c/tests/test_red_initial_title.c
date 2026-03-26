/* test_red_initial_title.c — RED tests for initial window title
 *
 * The window title should show the shell/profile name immediately at startup,
 * before any PTY output arrives. The fallback chain is:
 *   profile->name → basename of shell exe → "Wixen Terminal" (bare)
 *
 * Once the shell emits an OSC 0/2 title sequence, it overrides the initial title.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/window.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

/* Test 1: Default profile = PowerShell -> initial title contains "PowerShell" */
TEST red_default_profile_powershell_title(void) {
    char *title = wixen_format_window_title("PowerShell", NULL);
    ASSERT(title != NULL);
    ASSERT(strstr(title, "PowerShell") != NULL);
    ASSERT(strstr(title, "Wixen Terminal") != NULL);
    free(title);
    PASS();
}

/* Test 2: Detected shell = pwsh.exe but no profile -> title contains "pwsh" */
TEST red_detected_shell_pwsh_title(void) {
    char *title = wixen_format_window_title("pwsh", NULL);
    ASSERT(title != NULL);
    ASSERT(strstr(title, "pwsh") != NULL);
    ASSERT(strstr(title, "Wixen Terminal") != NULL);
    free(title);
    PASS();
}

/* Test 3: If OSC title later arrives, it overrides the initial title.
 * We verify by formatting a new title with the OSC payload and checking
 * it differs from the initial one. */
TEST red_osc_title_overrides_initial(void) {
    char *initial = wixen_format_window_title("PowerShell", NULL);
    ASSERT(initial != NULL);

    /* Simulate OSC title override — use wixen_window_format_title which is
     * what main.c uses when ps->terminal.title_dirty fires. */
    char *osc_title = wixen_window_format_title("Administrator: pwsh", NULL);
    ASSERT(osc_title != NULL);

    /* The OSC title should be different from the initial title */
    ASSERT(strcmp(initial, osc_title) != 0);

    /* The OSC title should contain the OSC payload, not the profile name */
    ASSERT(strstr(osc_title, "Administrator: pwsh") != NULL);

    free(initial);
    free(osc_title);
    PASS();
}

/* Test 4: Title format is "{shell} \xe2\x80\x94 Wixen Terminal" (em-dash) */
TEST red_title_format_em_dash(void) {
    char *title = wixen_format_window_title("cmd", NULL);
    ASSERT(title != NULL);
    /* Expect "cmd \xe2\x80\x94 Wixen Terminal" — UTF-8 em-dash is 0xE2 0x80 0x94 */
    ASSERT_STR_EQ("cmd \xe2\x80\x94 Wixen Terminal", title);
    free(title);
    PASS();
}

/* Test 5: No profile and no shell -> title is just "Wixen Terminal" */
TEST red_no_shell_fallback_title(void) {
    char *title = wixen_format_window_title(NULL, NULL);
    ASSERT(title != NULL);
    ASSERT_STR_EQ("Wixen Terminal", title);
    free(title);
    PASS();
}

SUITE(red_initial_title) {
    RUN_TEST(red_default_profile_powershell_title);
    RUN_TEST(red_detected_shell_pwsh_title);
    RUN_TEST(red_osc_title_overrides_initial);
    RUN_TEST(red_title_format_em_dash);
    RUN_TEST(red_no_shell_fallback_title);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_initial_title);
    GREATEST_MAIN_END();
}
