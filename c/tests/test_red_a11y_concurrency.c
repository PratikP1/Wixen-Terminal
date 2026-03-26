/* test_red_a11y_concurrency.c — RED-GREEN TDD for unified a11y concurrency contract
 *
 * Verifies the thread-safe accessibility state contract:
 * - Text snapshot read after update returns updated text
 * - Focus read after focus update returns correct value
 * - Cursor offset read via InterlockedExchange is coherent
 * - Sequential update->read cycles preserve all values
 * - has_focus + cursor + text are all coherent after a combined update
 *
 * These tests exercise the SRWLOCK-protected fields and the lock-free
 * cursor_offset_utf16 path to ensure the concurrency contract holds.
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/a11y/state.h"

#include <windows.h>

/* =========================================================
 * 1. Text snapshot read after update returns updated text
 * ========================================================= */

TEST text_snapshot_after_update(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Initial state should be empty string */
    char buf[256];
    size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_EQ(0, (int)len);
    ASSERT_STR_EQ("", buf);

    /* Update text and read back */
    wixen_a11y_state_set_text(state, "Hello, screen reader");
    len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ("Hello, screen reader", buf);
    ASSERT_EQ(20, (int)len);

    /* Update again — old value must be fully replaced */
    wixen_a11y_state_set_text(state, "New content");
    len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ("New content", buf);
    ASSERT_EQ(11, (int)len);

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST text_snapshot_with_newlines(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    wixen_a11y_state_set_text(state, "line1\nline2\nline3");
    char buf[256];
    size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ("line1\nline2\nline3", buf);
    ASSERT_EQ(17, (int)len);

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST text_snapshot_null_input(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Setting NULL text should be safe and treated as empty */
    wixen_a11y_state_set_text(state, NULL);
    char buf[256];
    size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_EQ(0, (int)len);
    ASSERT_STR_EQ("", buf);

    wixen_a11y_state_destroy(state);
    PASS();
}

/* =========================================================
 * 2. Focus read after focus update returns correct value
 * ========================================================= */

TEST focus_read_after_update(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Default should be false (calloc zeros the struct) */
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));

    /* Set focus true, read back */
    wixen_a11y_state_update_focus(state, true);
    ASSERT(wixen_a11y_state_has_focus(state));

    /* Set focus false, read back */
    wixen_a11y_state_update_focus(state, false);
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));

    /* Toggle rapidly */
    for (int i = 0; i < 100; i++) {
        bool expected = (i % 2 == 0);
        wixen_a11y_state_update_focus(state, expected);
        ASSERT_EQ(expected, wixen_a11y_state_has_focus(state));
    }

    wixen_a11y_state_destroy(state);
    PASS();
}

/* =========================================================
 * 3. Cursor offset read via InterlockedExchange is coherent
 * ========================================================= */

TEST cursor_offset_coherent(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Initial offset should be 0 */
    ASSERT_EQ(0, (int)wixen_a11y_state_cursor_offset(state));

    /* Set text, then move cursor — offset should reflect position */
    wixen_a11y_state_set_text(state, "abcdef");
    wixen_a11y_state_update_cursor(state, 0, 3);
    int32_t offset = wixen_a11y_state_cursor_offset(state);
    ASSERT_EQ(3, (int)offset);

    /* Move to end of text */
    wixen_a11y_state_update_cursor(state, 0, 6);
    offset = wixen_a11y_state_cursor_offset(state);
    ASSERT_EQ(6, (int)offset);

    /* Move to beginning */
    wixen_a11y_state_update_cursor(state, 0, 0);
    offset = wixen_a11y_state_cursor_offset(state);
    ASSERT_EQ(0, (int)offset);

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST cursor_offset_multiline(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Multi-line text: "abc\ndef\nghi" */
    wixen_a11y_state_set_text(state, "abc\ndef\nghi");

    /* Row 0, col 2 -> offset 2 */
    wixen_a11y_state_update_cursor(state, 0, 2);
    ASSERT_EQ(2, (int)wixen_a11y_state_cursor_offset(state));

    /* Row 1, col 1 -> offset 5 (past "abc\n" = 4, then 1 more) */
    wixen_a11y_state_update_cursor(state, 1, 1);
    ASSERT_EQ(5, (int)wixen_a11y_state_cursor_offset(state));

    /* Row 2, col 0 -> offset 8 (past "abc\ndef\n" = 8) */
    wixen_a11y_state_update_cursor(state, 2, 0);
    ASSERT_EQ(8, (int)wixen_a11y_state_cursor_offset(state));

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST cursor_offset_is_nonnegative(void) {
    /* The lock-free read should never return a negative offset */
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    wixen_a11y_state_set_text(state, "test");
    for (size_t r = 0; r < 5; r++) {
        for (size_t c = 0; c < 10; c++) {
            wixen_a11y_state_update_cursor(state, r, c);
            ASSERT(wixen_a11y_state_cursor_offset(state) >= 0);
        }
    }

    wixen_a11y_state_destroy(state);
    PASS();
}

/* =========================================================
 * 4. Sequential update->read cycles preserve all values
 * ========================================================= */

TEST sequential_cycles_preserve_values(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    char buf[512];
    char expected[128];

    for (int i = 0; i < 50; i++) {
        snprintf(expected, sizeof(expected), "Iteration %d text content", i);
        wixen_a11y_state_set_text(state, expected);
        wixen_a11y_state_update_cursor(state, (size_t)(i % 10), (size_t)(i % 40));
        wixen_a11y_state_update_focus(state, (i % 2 == 0));

        /* Read back — all values must match what was written */
        size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
        ASSERT_STR_EQ(expected, buf);
        ASSERT_EQ(strlen(expected), len);

        bool expected_focus = (i % 2 == 0);
        ASSERT_EQ(expected_focus, wixen_a11y_state_has_focus(state));

        /* Cursor offset should be non-negative and coherent */
        int32_t offset = wixen_a11y_state_cursor_offset(state);
        ASSERT(offset >= 0);
    }

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST sequential_text_overwrite_no_stale_data(void) {
    /* Ensure that shorter text fully replaces longer text */
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    wixen_a11y_state_set_text(state, "A very long string that takes up lots of space");
    char buf[256];
    wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ("A very long string that takes up lots of space", buf);

    /* Overwrite with shorter text */
    wixen_a11y_state_set_text(state, "Short");
    size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ("Short", buf);
    ASSERT_EQ(5, (int)len);

    wixen_a11y_state_destroy(state);
    PASS();
}

/* =========================================================
 * 5. Combined update: has_focus + cursor + text coherent
 * ========================================================= */

TEST combined_update_coherence(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    /* Simulate a frame update: text + cursor + focus all change together */
    const char *prompt = "PS C:\\Users> dir";
    size_t prompt_len = strlen(prompt);  /* 16 (single backslash in C string) */
    wixen_a11y_state_set_text(state, prompt);
    wixen_a11y_state_update_cursor(state, 0, prompt_len);
    wixen_a11y_state_update_focus(state, true);

    /* All three must be coherent when read */
    char buf[256];
    size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ(prompt, buf);
    ASSERT_EQ((int)prompt_len, (int)len);

    ASSERT(wixen_a11y_state_has_focus(state));

    int32_t offset = wixen_a11y_state_cursor_offset(state);
    /* Cursor at row 0, col prompt_len on single-line text -> offset == prompt_len */
    ASSERT_EQ((int)prompt_len, (int)offset);

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST combined_update_focus_lost(void) {
    /* When focus is lost, state should still be readable */
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    wixen_a11y_state_set_text(state, "Some terminal output");
    wixen_a11y_state_update_cursor(state, 0, 5);
    wixen_a11y_state_update_focus(state, true);

    /* Now lose focus */
    wixen_a11y_state_update_focus(state, false);

    /* Text and cursor should still be valid */
    char buf[256];
    wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_STR_EQ("Some terminal output", buf);

    ASSERT_FALSE(wixen_a11y_state_has_focus(state));
    ASSERT_EQ(5, (int)wixen_a11y_state_cursor_offset(state));

    wixen_a11y_state_destroy(state);
    PASS();
}

TEST combined_rapid_updates(void) {
    /* Rapid combined updates should never produce inconsistent state
     * when read from the same thread (single-threaded coherence) */
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);

    char buf[256];
    char text[64];

    for (int i = 0; i < 200; i++) {
        snprintf(text, sizeof(text), "Line %d", i);
        bool focus = (i % 3 != 0);
        size_t col = (size_t)(i % 20);

        wixen_a11y_state_set_text(state, text);
        wixen_a11y_state_update_cursor(state, 0, col);
        wixen_a11y_state_update_focus(state, focus);

        /* Immediate readback must be coherent */
        size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
        ASSERT_STR_EQ(text, buf);
        ASSERT_EQ(strlen(text), len);
        ASSERT_EQ(focus, wixen_a11y_state_has_focus(state));

        int32_t offset = wixen_a11y_state_cursor_offset(state);
        ASSERT(offset >= 0);
        /* For single-line text, offset should equal col (capped at text length) */
        int32_t expected_offset = (int32_t)(col <= strlen(text) ? col : strlen(text));
        ASSERT_EQ(expected_offset, offset);
    }

    wixen_a11y_state_destroy(state);
    PASS();
}

/* =========================================================
 * Suites
 * ========================================================= */

SUITE(text_snapshot) {
    RUN_TEST(text_snapshot_after_update);
    RUN_TEST(text_snapshot_with_newlines);
    RUN_TEST(text_snapshot_null_input);
}

SUITE(focus_contract) {
    RUN_TEST(focus_read_after_update);
}

SUITE(cursor_offset_contract) {
    RUN_TEST(cursor_offset_coherent);
    RUN_TEST(cursor_offset_multiline);
    RUN_TEST(cursor_offset_is_nonnegative);
}

SUITE(sequential_cycles) {
    RUN_TEST(sequential_cycles_preserve_values);
    RUN_TEST(sequential_text_overwrite_no_stale_data);
}

SUITE(combined_coherence) {
    RUN_TEST(combined_update_coherence);
    RUN_TEST(combined_update_focus_lost);
    RUN_TEST(combined_rapid_updates);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(text_snapshot);
    RUN_SUITE(focus_contract);
    RUN_SUITE(cursor_offset_contract);
    RUN_SUITE(sequential_cycles);
    RUN_SUITE(combined_coherence);
    GREATEST_MAIN_END();
}

#else
/* Non-Windows: skip a11y concurrency tests */
#include "greatest.h"
GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    GREATEST_MAIN_END();
}
#endif
