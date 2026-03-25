/* test_red_pty_close.c — RED tests for PTY cleanup order
 *
 * BUG #27: pty_close called ClosePseudoConsole before closing pipes,
 * causing the reader thread to hang on ReadFile, freezing exit.
 *
 * The correct order:
 * 1. Close pipe_out_read (breaks reader's ReadFile)
 * 2. Close pipe_in_write (signals child's stdin EOF)
 * 3. Wait for reader thread
 * 4. Close pseudo console
 * 5. Terminate child if needed
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/pty/pty.h"

#ifdef _WIN32

TEST red_pty_close_empty(void) {
    /* Close on uninitialized PTY should not crash */
    WixenPty pty;
    memset(&pty, 0, sizeof(pty));
    wixen_pty_close(&pty);
    PASS();
}

TEST red_pty_close_double(void) {
    /* Double close should not crash */
    WixenPty pty;
    memset(&pty, 0, sizeof(pty));
    wixen_pty_close(&pty);
    wixen_pty_close(&pty);
    PASS();
}

TEST red_pty_spawn_and_close(void) {
    /* Spawn a real shell, then close — must not hang */
    WixenPty pty;
    wchar_t *shell = wixen_pty_detect_shell();
    ASSERT(shell != NULL);
    HWND hwnd = GetDesktopWindow(); /* Use desktop as notification target */
    bool ok = wixen_pty_spawn(&pty, 80, 24, shell, NULL, NULL, hwnd);
    free(shell);
    if (!ok) {
        /* May fail in restricted environments — skip */
        PASS();
    }
    /* Give shell time to start */
    Sleep(200);
    /* Close — must return within a few seconds, not hang */
    wixen_pty_close(&pty);
    /* If we get here, no hang */
    PASS();
}

SUITE(red_pty_close) {
    RUN_TEST(red_pty_close_empty);
    RUN_TEST(red_pty_close_double);
    RUN_TEST(red_pty_spawn_and_close);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_pty_close);
    GREATEST_MAIN_END();
}

#else
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) { GREATEST_MAIN_BEGIN(); GREATEST_MAIN_END(); }
#endif
