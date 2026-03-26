/* test_red_com_latency.c — RED tests for COM dispatch latency
 *
 * UseComThreading means ALL UIA provider method calls (GetSelection,
 * GetCaretRange, GetText, ExpandToEnclosingUnit) arrive as Win32 messages.
 * They can ONLY be dispatched during PeekMessage/pump_messages.
 *
 * If we pump rarely, NVDA waits ~1 second per call. The frame loop must
 * pump aggressively: before window events, after a11y updates, before
 * render, and after render.
 *
 * These tests verify:
 * 1. pump_messages is called frequently during frame processing
 * 2. pump points exist BEFORE heavy work (PTY, rendering)
 * 3. wixen_a11y_pump_messages exists and is callable
 * 4. Sufficient pump_messages calls per frame (>= 3)
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"

#ifdef _WIN32
#include <windows.h>
#include "wixen/a11y/provider.h"
#endif

/* Helper: read main.c source and count occurrences of a pattern in the
 * frame loop body (between "Main event loop" and "MsgWaitForMultipleObjects"
 * at the end of the loop). */
static int count_pattern_in_frame_loop(const char *pattern) {
    /* Find main.c relative to test binary location */
    const char *paths[] = {
        "../../src/main.c",
        "../src/main.c",
        "src/main.c",
        "../../../c/src/main.c",
        "c/src/main.c",
    };
    FILE *f = NULL;
    for (int i = 0; i < 5; i++) {
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) {
        /* Try absolute path as last resort */
        f = fopen("C:/Users/prati/Documents/projects/Wixen-Terminal/c/src/main.c", "r");
    }
    if (!f) return -1;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    /* Find frame loop boundaries */
    const char *loop_start = strstr(buf, "Main event loop");
    /* The final MsgWaitForMultipleObjects at the end of the while(running) loop */
    const char *loop_end = NULL;
    if (loop_start) {
        /* Find the LAST MsgWaitForMultipleObjects which is the frame sleep */
        const char *p = loop_start;
        while (1) {
            const char *next = strstr(p, "MsgWaitForMultipleObjects");
            if (!next) break;
            loop_end = next;
            p = next + 1;
        }
    }

    if (!loop_start || !loop_end) {
        free(buf);
        return -1;
    }

    /* Count pattern occurrences in the frame loop body */
    int count = 0;
    const char *p = loop_start;
    size_t plen = strlen(pattern);
    while (p < loop_end) {
        const char *found = strstr(p, pattern);
        if (!found || found >= loop_end) break;
        count++;
        p = found + plen;
    }

    free(buf);
    return count;
}

/* Helper: check if a pattern appears BEFORE another pattern in the frame loop */
static bool pattern_before(const char *first, const char *second) {
    const char *paths[] = {
        "../../src/main.c",
        "../src/main.c",
        "src/main.c",
        "../../../c/src/main.c",
        "c/src/main.c",
    };
    FILE *f = NULL;
    for (int i = 0; i < 5; i++) {
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) {
        f = fopen("C:/Users/prati/Documents/projects/Wixen-Terminal/c/src/main.c", "r");
    }
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    /* Search within the frame loop */
    const char *loop_start = strstr(buf, "Main event loop");
    if (!loop_start) { free(buf); return false; }

    const char *f1 = strstr(loop_start, first);
    const char *f2 = strstr(loop_start, second);

    bool result = (f1 && f2 && f1 < f2);
    free(buf);
    return result;
}

/* Test 1: pump_messages is called at least every 16ms during frame processing.
 * We verify this structurally by checking that pump_messages (or
 * wixen_a11y_pump_messages) appears multiple times in the frame loop,
 * surrounding the heavy work sections. */
TEST red_pump_frequency_in_frame_loop(void) {
    /* Count all pump calls: both pump_messages() and wixen_a11y_pump_messages() */
    int pump_count = count_pattern_in_frame_loop("pump_messages");
    ASSERTm("pump_messages should appear >= 3 times in the frame loop",
            pump_count >= 3);
    PASS();
}

/* Test 2: pump points exist BEFORE heavy work.
 * The frame loop must pump BEFORE rendering so COM calls queued during
 * PTY processing get dispatched without waiting for the full render. */
TEST red_pump_before_render(void) {
    /* There should be a pump_messages call between window event processing
     * and wixen_renderer_render_frame */
    bool pump_before_render = pattern_before(
        "/* COM pump: before render",
        "wixen_renderer_render_frame");
    ASSERTm("pump_messages must appear BEFORE render to dispatch queued COM calls",
            pump_before_render);
    PASS();
}

/* Test 2b: pump point exists AFTER render so COM calls queued during
 * render get dispatched immediately. */
TEST red_pump_after_render(void) {
    bool pump_after_render = pattern_before(
        "wixen_renderer_render_frame",
        "/* COM pump: after render");
    ASSERTm("pump_messages must appear AFTER render to dispatch COM calls queued during render",
            pump_after_render);
    PASS();
}

/* Test 2c: pump point exists AFTER a11y state updates so GetSelection/
 * GetCaretRange respond to fresh state. */
TEST red_pump_after_a11y_updates(void) {
    /* The a11y pump should come after cursor offset computation */
    bool pump_after_cursor = pattern_before(
        "wixen_a11y_update_cursor",
        "wixen_a11y_pump_messages");
    ASSERTm("pump must come after a11y state updates for fresh GetSelection data",
            pump_after_cursor);
    PASS();
}

/* Test 3: wixen_a11y_pump_messages exists and is callable.
 * This function is declared in provider.h and must be available to link. */
TEST red_a11y_pump_messages_exists(void) {
#ifdef _WIN32
    /* Just verify the function pointer is non-null (it's a real function) */
    void (*fn)(HWND) = wixen_a11y_pump_messages;
    ASSERT(fn != NULL);
#endif
    PASS();
}

/* Test 4: Count pump_messages calls in the frame loop body.
 * With aggressive pumping we need at least:
 *   1. Before window events (dispatch COM queued during PTY)
 *   2. Before render
 *   3. After render
 *   4. After a11y state updates (existing wixen_a11y_pump_messages)
 * So >= 4 total pump points. We test for >= 3 as minimum. */
TEST red_pump_count_minimum(void) {
    int pump_count = count_pattern_in_frame_loop("pump_messages");
    ASSERT_GTE(pump_count, 4);
    PASS();
}

/* Test 5: MsgWaitForMultipleObjects at end of loop must use QS_ALLINPUT
 * (not just QS_POSTMESSAGE) so COM messages wake the thread. */
TEST red_msgwait_uses_allinput(void) {
    int allinput_count = count_pattern_in_frame_loop("QS_ALLINPUT");
    ASSERT_GTE(allinput_count, 1);
    PASS();
}

SUITE(com_latency_suite) {
    RUN_TEST(red_pump_frequency_in_frame_loop);
    RUN_TEST(red_pump_before_render);
    RUN_TEST(red_pump_after_render);
    RUN_TEST(red_pump_after_a11y_updates);
    RUN_TEST(red_a11y_pump_messages_exists);
    RUN_TEST(red_pump_count_minimum);
    RUN_TEST(red_msgwait_uses_allinput);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(com_latency_suite);
    GREATEST_MAIN_END();
}
