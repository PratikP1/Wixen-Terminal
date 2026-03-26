/* test_red_shutdown.c — RED tests for shutdown lifecycle state machine
 *
 * P0.3: Normal exit should NOT use TerminateProcess. The shutdown state
 * machine tracks phases (RUNNING -> SHUTTING_DOWN -> COMPLETE) so the
 * watchdog only force-kills when cleanup genuinely stalls.
 */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/shutdown.h"

/* After begin + mark_complete, is_complete must return true. */
TEST red_begin_complete_is_complete(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    wixen_shutdown_begin(&st);
    wixen_shutdown_mark_complete(&st);
    ASSERT(wixen_shutdown_is_complete(&st));
    PASS();
}

/* Completed shutdown must NOT trigger force-kill. */
TEST red_completed_does_not_force_kill(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    wixen_shutdown_begin(&st);
    wixen_shutdown_mark_complete(&st);
    ASSERT_FALSE(wixen_shutdown_should_force_kill(&st, 3000));
    PASS();
}

/* Timeout without completion DOES trigger force-kill. */
TEST red_timeout_without_complete_force_kills(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    wixen_shutdown_begin(&st);
    /* GetTickCount() has ~15ms resolution on Windows, so sleep enough
     * to guarantee elapsed > 0. */
    Sleep(50);
    ASSERT(wixen_shutdown_should_force_kill(&st, 0));
    PASS();
}

/* should_force_kill returns false when shutdown is complete,
 * even if timeout would have been exceeded. */
TEST red_should_force_kill_false_when_complete(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    wixen_shutdown_begin(&st);
    Sleep(50);
    wixen_shutdown_mark_complete(&st);
    /* Even with timeout_ms=0, complete overrides. */
    ASSERT_FALSE(wixen_shutdown_should_force_kill(&st, 0));
    PASS();
}

/* Calling mark_complete multiple times is safe. */
TEST red_repeated_mark_complete_is_safe(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    wixen_shutdown_begin(&st);
    wixen_shutdown_mark_complete(&st);
    wixen_shutdown_mark_complete(&st);
    wixen_shutdown_mark_complete(&st);
    ASSERT(wixen_shutdown_is_complete(&st));
    ASSERT_FALSE(wixen_shutdown_should_force_kill(&st, 3000));
    PASS();
}

/* elapsed_ms returns a non-negative value after begin. */
TEST red_elapsed_ms_after_begin(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    wixen_shutdown_begin(&st);
    Sleep(50);
    DWORD elapsed = wixen_shutdown_elapsed_ms(&st);
    ASSERT(elapsed >= 1); /* At least some time has passed */
    PASS();
}

/* should_force_kill returns false when still in RUNNING phase. */
TEST red_should_force_kill_false_when_running(void) {
    WixenShutdownState st;
    wixen_shutdown_init(&st);
    /* Never called begin(), still RUNNING */
    ASSERT_FALSE(wixen_shutdown_should_force_kill(&st, 0));
    PASS();
}

SUITE(red_shutdown) {
    RUN_TEST(red_begin_complete_is_complete);
    RUN_TEST(red_completed_does_not_force_kill);
    RUN_TEST(red_timeout_without_complete_force_kills);
    RUN_TEST(red_should_force_kill_false_when_complete);
    RUN_TEST(red_repeated_mark_complete_is_safe);
    RUN_TEST(red_elapsed_ms_after_begin);
    RUN_TEST(red_should_force_kill_false_when_running);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_shutdown);
    GREATEST_MAIN_END();
}
