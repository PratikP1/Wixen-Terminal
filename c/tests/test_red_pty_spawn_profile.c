/* test_red_pty_spawn_profile.c — RED tests for PTY spawning from profile
 *
 * Verify that the default profile's shell actually spawns and produces output.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/config/config.h"

#ifdef _WIN32
#include <windows.h>
#include "wixen/pty/pty.h"

TEST red_profile_shell_exists_on_path(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *prof = wixen_config_default_profile(&cfg);
    ASSERT(prof != NULL);
    ASSERT(prof->program != NULL);

    /* Convert to wide and search PATH */
    wchar_t wprogram[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, prof->program, -1, wprogram, MAX_PATH);
    wchar_t found[MAX_PATH];
    BOOL on_path = SearchPathW(NULL, wprogram, L".exe", MAX_PATH, found, NULL);

    printf("  Profile: %s\n", prof->name);
    printf("  Program: %s\n", prof->program);
    printf("  On PATH: %s\n", on_path ? "YES" : "NO");
    if (on_path) printf("  Found at: %ls\n", found);

    ASSERT(on_path);
    wixen_config_free(&cfg);
    PASS();
}

TEST red_pty_spawn_profile_shell(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    const WixenProfile *prof = wixen_config_default_profile(&cfg);
    ASSERT(prof != NULL);

    wchar_t wprogram[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, prof->program, -1, wprogram, MAX_PATH);

    WixenPty pty;
    /* Create a hidden window for notifications */
    bool spawned = wixen_pty_spawn(&pty, 80, 24, wprogram, NULL, NULL, NULL);
    printf("  Spawned: %s\n", spawned ? "YES" : "NO");
    if (!spawned) {
        printf("  GetLastError: %lu\n", GetLastError());
    }
    ASSERT(spawned);

    /* Wait briefly for output */
    Sleep(2000);

    /* Read whatever the shell produced */
    char buf[4096] = {0};
    DWORD bytes_read = 0;
    DWORD avail = 0;
    PeekNamedPipe(pty.pipe_out_read, NULL, 0, NULL, &avail, NULL);
    printf("  Bytes available: %lu\n", avail);

    if (avail > 0) {
        ReadFile(pty.pipe_out_read, buf, sizeof(buf) - 1, &bytes_read, NULL);
        buf[bytes_read] = '\0';
        printf("  First %lu bytes of output: [%.100s...]\n", bytes_read, buf);
    }

    ASSERT(avail > 0); /* Shell should produce SOMETHING (prompt, banner) */

    wixen_pty_close(&pty);
    wixen_config_free(&cfg);
    PASS();
}

#else
TEST red_profile_shell_exists_on_path(void) { SKIP(); PASS(); }
TEST red_pty_spawn_profile_shell(void) { SKIP(); PASS(); }
#endif

SUITE(red_pty_spawn_profile) {
    RUN_TEST(red_profile_shell_exists_on_path);
    RUN_TEST(red_pty_spawn_profile_shell);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_pty_spawn_profile);
    GREATEST_MAIN_END();
}
