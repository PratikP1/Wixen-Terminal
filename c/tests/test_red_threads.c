/* test_red_threads.c — RED tests for thread safety
 *
 * The a11y state is read by UIA threads (GetSelection, GetText) while
 * the main thread writes (cursor position, full_text). The SRWLOCK
 * must protect all access. These tests hammer concurrent read/write
 * to detect races, deadlocks, and torn reads.
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/a11y/state.h"

#include <windows.h>

#define NUM_READERS 4
#define NUM_ITERATIONS 10000

typedef struct {
    WixenA11yState *state;
    volatile LONG errors;
    volatile LONG done;
} ThreadCtx;

static DWORD WINAPI reader_thread(LPVOID param) {
    ThreadCtx *ctx = (ThreadCtx *)param;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        /* Simulate UIA GetSelection — read cursor offset */
        int32_t offset = wixen_a11y_state_cursor_offset(ctx->state);
        if (offset < 0) {
            InterlockedIncrement(&ctx->errors);
        }
        /* Simulate UIA GetText — read visible text */
        char buf[256];
        size_t len = wixen_a11y_state_get_text(ctx->state, buf, sizeof(buf));
        /* Text should be consistent (not a torn read) */
        for (size_t j = 0; j < len && j < sizeof(buf) - 1; j++) {
            /* Each char should be printable or newline */
            if (buf[j] != '\n' && (buf[j] < 0x20 || buf[j] > 0x7E)) {
                /* Torn read — got garbage */
                InterlockedIncrement(&ctx->errors);
                break;
            }
        }
    }
    InterlockedIncrement(&ctx->done);
    return 0;
}

static DWORD WINAPI writer_thread(LPVOID param) {
    ThreadCtx *ctx = (ThreadCtx *)param;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        /* Simulate main thread updating cursor + text */
        wixen_a11y_state_update_cursor(ctx->state, (size_t)(i % 24), (size_t)(i % 80));
        /* Update text with consistent content */
        char text[128];
        snprintf(text, sizeof(text), "Line %d content here", i);
        wixen_a11y_state_set_text(ctx->state, text);
    }
    InterlockedIncrement(&ctx->done);
    return 0;
}

TEST red_concurrent_read_write(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Set initial state */
    wixen_a11y_state_set_text(state, "Initial text");
    wixen_a11y_state_update_cursor(state, 0, 0);

    ThreadCtx ctx = { .state = state, .errors = 0, .done = 0 };

    /* Launch readers + writer */
    HANDLE threads[NUM_READERS + 1];
    for (int i = 0; i < NUM_READERS; i++) {
        threads[i] = CreateThread(NULL, 0, reader_thread, &ctx, 0, NULL);
    }
    threads[NUM_READERS] = CreateThread(NULL, 0, writer_thread, &ctx, 0, NULL);

    /* Wait for all */
    WaitForMultipleObjects(NUM_READERS + 1, threads, TRUE, 10000);
    for (int i = 0; i <= NUM_READERS; i++) {
        CloseHandle(threads[i]);
    }

    /* Check results */
    ASSERT_EQ(0, (int)ctx.errors);
    ASSERT_EQ(NUM_READERS + 1, (int)ctx.done);

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST red_concurrent_multi_writer(void) {
    /* Two writers + readers — should not deadlock */
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);
    wixen_a11y_state_set_text(state, "Start");

    ThreadCtx ctx = { .state = state, .errors = 0, .done = 0 };

    HANDLE threads[6];
    threads[0] = CreateThread(NULL, 0, writer_thread, &ctx, 0, NULL);
    threads[1] = CreateThread(NULL, 0, writer_thread, &ctx, 0, NULL);
    threads[2] = CreateThread(NULL, 0, reader_thread, &ctx, 0, NULL);
    threads[3] = CreateThread(NULL, 0, reader_thread, &ctx, 0, NULL);
    threads[4] = CreateThread(NULL, 0, reader_thread, &ctx, 0, NULL);
    threads[5] = CreateThread(NULL, 0, reader_thread, &ctx, 0, NULL);

    DWORD result = WaitForMultipleObjects(6, threads, TRUE, 15000);
    for (int i = 0; i < 6; i++) CloseHandle(threads[i]);

    /* Should not timeout (deadlock) */
    ASSERT(result != WAIT_TIMEOUT);
    ASSERT_EQ(0, (int)ctx.errors);

    wixen_a11y_state_destroy(state);
    PASS();
}

SUITE(red_threads) {
    RUN_TEST(red_concurrent_read_write);
    RUN_TEST(red_concurrent_multi_writer);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_threads);
    GREATEST_MAIN_END();
}

#else
/* Non-Windows: skip thread tests */
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    GREATEST_MAIN_END();
}
#endif
