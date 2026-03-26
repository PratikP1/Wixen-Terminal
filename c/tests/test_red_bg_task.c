/* test_red_bg_task.c — RED tests for WixenBgTask lifecycle
 *
 * Validates that CreateThread handles are stored, tracked,
 * and properly closed — preventing handle leaks.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/core/bg_task.h"

/* Trivial worker: marks done immediately. */
static DWORD WINAPI dummy_worker(LPVOID param) {
    WixenBgTask *t = (WixenBgTask *)param;
    Sleep(10); /* Small delay so "start" returns first */
    wixen_bg_task_mark_done(t);
    return 0;
}

/* Slow worker: sleeps then marks done. */
static DWORD WINAPI slow_worker(LPVOID param) {
    WixenBgTask *t = (WixenBgTask *)param;
    Sleep(200);
    wixen_bg_task_mark_done(t);
    return 0;
}

TEST red_bg_task_init_zeroes(void) {
    WixenBgTask t;
    /* Poison the struct first */
    memset(&t, 0xFF, sizeof(t));
    wixen_bg_task_init(&t);

    ASSERT_EQ(NULL, t.thread);
    ASSERT_EQ(0, t.done);
    ASSERT_EQ(0, t.consumed);
    PASS();
}

TEST red_bg_task_start_stores_handle(void) {
    WixenBgTask t;
    wixen_bg_task_init(&t);

    bool ok = wixen_bg_task_start(&t, dummy_worker, &t);
    ASSERT(ok);
    ASSERT(t.thread != NULL);

    /* Wait for it to finish, then clean up */
    WaitForSingleObject(t.thread, 2000);
    wixen_bg_task_finalize(&t);
    PASS();
}

TEST red_bg_task_mark_done_is_done(void) {
    WixenBgTask t;
    wixen_bg_task_init(&t);

    ASSERT_FALSE(wixen_bg_task_is_done(&t));

    wixen_bg_task_mark_done(&t);
    ASSERT(wixen_bg_task_is_done(&t));

    PASS();
}

TEST red_bg_task_finalize_closes_handle(void) {
    WixenBgTask t;
    wixen_bg_task_init(&t);
    wixen_bg_task_start(&t, dummy_worker, &t);

    /* Wait for worker to finish */
    WaitForSingleObject(t.thread, 2000);

    wixen_bg_task_finalize(&t);
    ASSERT_EQ(1, t.consumed);
    ASSERT_EQ(NULL, t.thread);
    PASS();
}

TEST red_bg_task_double_finalize_safe(void) {
    WixenBgTask t;
    wixen_bg_task_init(&t);
    wixen_bg_task_start(&t, dummy_worker, &t);

    WaitForSingleObject(t.thread, 2000);

    wixen_bg_task_finalize(&t);
    ASSERT_EQ(1, t.consumed);

    /* Second finalize must not crash or double-close */
    wixen_bg_task_finalize(&t);
    ASSERT_EQ(1, t.consumed);
    PASS();
}

TEST red_bg_task_cleanup_after_finalize_safe(void) {
    WixenBgTask t;
    wixen_bg_task_init(&t);
    wixen_bg_task_start(&t, dummy_worker, &t);

    WaitForSingleObject(t.thread, 2000);

    wixen_bg_task_finalize(&t);
    /* Cleanup after finalize must be a no-op */
    wixen_bg_task_cleanup(&t);
    ASSERT_EQ(1, t.consumed);
    ASSERT_EQ(NULL, t.thread);
    PASS();
}

TEST red_bg_task_cleanup_without_finalize(void) {
    /* If finalize was never called, cleanup should close the handle */
    WixenBgTask t;
    wixen_bg_task_init(&t);
    wixen_bg_task_start(&t, dummy_worker, &t);

    WaitForSingleObject(t.thread, 2000);

    ASSERT_EQ(0, t.consumed);
    wixen_bg_task_cleanup(&t);
    ASSERT_EQ(NULL, t.thread);
    PASS();
}

#else
/* Non-Windows stubs */
TEST red_bg_task_init_zeroes(void) { SKIP(); PASS(); }
TEST red_bg_task_start_stores_handle(void) { SKIP(); PASS(); }
TEST red_bg_task_mark_done_is_done(void) { SKIP(); PASS(); }
TEST red_bg_task_finalize_closes_handle(void) { SKIP(); PASS(); }
TEST red_bg_task_double_finalize_safe(void) { SKIP(); PASS(); }
TEST red_bg_task_cleanup_after_finalize_safe(void) { SKIP(); PASS(); }
TEST red_bg_task_cleanup_without_finalize(void) { SKIP(); PASS(); }
#endif

SUITE(red_bg_task) {
    RUN_TEST(red_bg_task_init_zeroes);
    RUN_TEST(red_bg_task_start_stores_handle);
    RUN_TEST(red_bg_task_mark_done_is_done);
    RUN_TEST(red_bg_task_finalize_closes_handle);
    RUN_TEST(red_bg_task_double_finalize_safe);
    RUN_TEST(red_bg_task_cleanup_after_finalize_safe);
    RUN_TEST(red_bg_task_cleanup_without_finalize);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_bg_task);
    GREATEST_MAIN_END();
}
