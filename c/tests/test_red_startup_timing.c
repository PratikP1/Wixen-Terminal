/* test_red_startup_timing.c — RED tests for startup timing measurement.
 *
 * Validates WixenStartupTimer: phase timestamps, non-negative elapsed,
 * and total startup time reporting.
 */
#include "greatest.h"
#include "wixen/core/startup_timer.h"
#include <string.h>

/* -------------------------------------------------------------------
 * TEST 1: Timer can be initialized and records phase timestamps
 * ------------------------------------------------------------------- */
TEST timer_records_phases(void) {
    WixenStartupTimer timer;
    wixen_timer_init(&timer);

    /* Mark several phases */
    wixen_timer_mark(&timer, "window");
    wixen_timer_mark(&timer, "renderer_bg");
    wixen_timer_mark(&timer, "pty");

    /* Timer should have recorded 3 phases (plus the init baseline) */
    ASSERT(timer.count >= 3);
    PASS();
}

/* -------------------------------------------------------------------
 * TEST 2: Elapsed time between phases is non-negative
 * ------------------------------------------------------------------- */
TEST elapsed_between_phases_non_negative(void) {
    WixenStartupTimer timer;
    wixen_timer_init(&timer);

    wixen_timer_mark(&timer, "first");
    /* Burn a tiny bit of time so marks aren't identical */
    for (volatile int i = 0; i < 100000; i++) {}
    wixen_timer_mark(&timer, "second");

    /* Each phase timestamp must be >= the previous one */
    for (size_t i = 1; i < timer.count; i++) {
        ASSERT(timer.marks[i].timestamp_us >= timer.marks[i - 1].timestamp_us);
    }
    PASS();
}

/* -------------------------------------------------------------------
 * TEST 3: Total startup time is non-negative and reasonable
 * ------------------------------------------------------------------- */
TEST total_startup_time_reported(void) {
    WixenStartupTimer timer;
    wixen_timer_init(&timer);

    wixen_timer_mark(&timer, "window");
    for (volatile int i = 0; i < 100000; i++) {}
    wixen_timer_mark(&timer, "ready");

    double total = wixen_timer_total_ms(&timer);
    /* Total must be non-negative */
    ASSERT(total >= 0.0);

    PASS();
}

/* -------------------------------------------------------------------
 * TEST 4: wixen_timer_log does not crash (smoke test)
 * ------------------------------------------------------------------- */
TEST timer_log_does_not_crash(void) {
    WixenStartupTimer timer;
    wixen_timer_init(&timer);

    wixen_timer_mark(&timer, "window");
    wixen_timer_mark(&timer, "renderer_bg");
    wixen_timer_mark(&timer, "pty");
    wixen_timer_mark(&timer, "renderer_done");
    wixen_timer_mark(&timer, "ready");

    /* Should not crash — writes to wixen-startup.log */
    wixen_timer_log(&timer, "wixen-startup-test.log");

    PASS();
}

SUITE(startup_timing) {
    RUN_TEST(timer_records_phases);
    RUN_TEST(elapsed_between_phases_non_negative);
    RUN_TEST(total_startup_time_reported);
    RUN_TEST(timer_log_does_not_crash);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(startup_timing);
    GREATEST_MAIN_END();
}
