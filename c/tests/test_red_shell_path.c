/* test_red_shell_path.c — RED test: profile program must be findable
 *
 * The default profile says "pwsh.exe" but if PowerShell 7 isn't
 * installed, SearchPathW won't find it and the PTY spawn fails silently.
 * The terminal opens but no shell runs inside it.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/config.h"
#include "wixen/pty/pty.h"

#ifdef _WIN32
#include <windows.h>

TEST red_detect_shell_finds_something(void) {
    wchar_t *shell = wixen_pty_detect_shell();
    ASSERT(shell != NULL);
    printf("  detect_shell => %ls\n", shell);
    free(shell);
    PASS();
}

TEST red_profile_program_on_path(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *p = wixen_config_default_profile(&cfg);
    ASSERT(p != NULL);
    printf("  profile: %s => %s\n", p->name, p->program);

    /* Convert to wide and search PATH */
    wchar_t wprogram[260];
    MultiByteToWideChar(CP_UTF8, 0, p->program, -1, wprogram, 260);
    wchar_t found[MAX_PATH];
    bool on_path = SearchPathW(NULL, wprogram, NULL, MAX_PATH, found, NULL) != 0;

    if (on_path) {
        printf("  FOUND: %ls\n", found);
    } else {
        printf("  NOT ON PATH: %ls (err=%lu)\n", wprogram, GetLastError());
        /* If default profile isn't on PATH, detect_shell should be used */
        wchar_t *fallback = wixen_pty_detect_shell();
        printf("  fallback: %ls\n", fallback ? fallback : L"(null)");
        ASSERT(fallback != NULL);
        free(fallback);
    }

    wixen_config_free(&cfg);
    PASS();
}

TEST red_all_profiles_resolvable(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    int found_count = 0;
    for (size_t i = 0; i < cfg.profile_count; i++) {
        wchar_t wp[260];
        MultiByteToWideChar(CP_UTF8, 0, cfg.profiles[i].program, -1, wp, 260);
        wchar_t found[MAX_PATH];
        if (SearchPathW(NULL, wp, NULL, MAX_PATH, found, NULL)) {
            printf("  [%zu] %s => %ls\n", i, cfg.profiles[i].name, found);
            found_count++;
        } else {
            printf("  [%zu] %s => NOT FOUND (%s)\n", i, cfg.profiles[i].name, cfg.profiles[i].program);
        }
    }
    /* At least one profile must be resolvable */
    printf("  %d of %zu profiles found on PATH\n", found_count, cfg.profile_count);
    ASSERT(found_count >= 1);
    wixen_config_free(&cfg);
    PASS();
}

SUITE(red_shell_path) {
    RUN_TEST(red_detect_shell_finds_something);
    RUN_TEST(red_profile_program_on_path);
    RUN_TEST(red_all_profiles_resolvable);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_shell_path);
    GREATEST_MAIN_END();
}

#else
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) { GREATEST_MAIN_BEGIN(); GREATEST_MAIN_END(); }
#endif
