/* test_red_a11y_perf_contract.c — Behavior-preserving regression tests for
 * a11y cursor offset + notification logic, prior to performance optimization.
 *
 * RED-GREEN TDD: locks down current behavior so perf refactors can't
 * silently break accessibility contracts.
 *
 * Covers:
 *   1. Cursor offset row 0 = just the column
 *   2. Cursor offset row N includes all previous row lengths + newlines
 *   3. Cursor offset across multiple rows with varying content
 *   4. No notification fires when terminal content unchanged between frames
 *   5. Notification fires on semantic change (new output)
 *   6. Large grid (200x50) cursor offset completes without error
 *   7. UTF-8 multibyte content doesn't break cursor offset
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/a11y/frame_update.h"
#include "wixen/a11y/text_boundaries.h"

/* --- Helper: build a WixenFrameA11yInput with defaults --- */
static WixenFrameA11yInput make_input(const char *text, size_t row, size_t col) {
    WixenFrameA11yInput inp;
    memset(&inp, 0, sizeof(inp));
    inp.visible_text = text;
    inp.cursor_row = row;
    inp.cursor_col = col;
    inp.now_ms = 1000;
    return inp;
}

/* --- Helper: free event heap allocations --- */
static void free_events(WixenFrameA11yEvents *ev) {
    free(ev->announce_text);
    free(ev->announce_line_text);
}

/* ================================================================
 * 1. Cursor offset for row 0 is just the column
 * ================================================================ */
TEST cursor_offset_row0_equals_col(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    WixenFrameA11yInput inp = make_input("Hello World", 0, 5);
    WixenFrameA11yEvents ev;
    wixen_frame_a11y_update(&fs, &inp, &ev);

    ASSERT_EQ(5, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Column 0 */
    inp = make_input("Hello World", 0, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(0, ev.cursor_offset_utf16);
    free_events(&ev);

    /* End of line */
    inp = make_input("Hello World", 0, 11);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(11, ev.cursor_offset_utf16);
    free_events(&ev);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * 2. Cursor offset for row N includes all previous row lengths + newlines
 * ================================================================ */
TEST cursor_offset_rowN_includes_previous_rows(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* "AAAA\nBBBBB\nCCC"
     * Row 0: 4 chars + 1 newline = 5 bytes
     * Row 1: 5 chars + 1 newline = 6 bytes
     * Row 2: col 2 => offset = 5 + 6 + 2 = 13
     */
    const char *text = "AAAA\nBBBBB\nCCC";
    WixenFrameA11yInput inp = make_input(text, 2, 2);
    WixenFrameA11yEvents ev;
    wixen_frame_a11y_update(&fs, &inp, &ev);

    ASSERT_EQ(13, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 1, col 0 => offset = 5 (past "AAAA\n") */
    inp = make_input(text, 1, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(5, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 1, col 3 => offset = 5 + 3 = 8 */
    inp = make_input(text, 1, 3);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(8, ev.cursor_offset_utf16);
    free_events(&ev);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * 3. Cursor offset across multiple rows with varying content
 * ================================================================ */
TEST cursor_offset_varying_row_lengths(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Short, medium, long, empty rows */
    const char *text = "A\nBCD\nEFGHIJ\n\nK";
    /* Row 0: "A"       len=1 + newline => 2 bytes
     * Row 1: "BCD"     len=3 + newline => 4 bytes
     * Row 2: "EFGHIJ"  len=6 + newline => 7 bytes
     * Row 3: ""        len=0 + newline => 1 byte
     * Row 4: "K"       len=1
     */
    WixenFrameA11yEvents ev;

    /* Row 4, col 0 => offset = 2 + 4 + 7 + 1 + 0 = 14 */
    WixenFrameA11yInput inp = make_input(text, 4, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(14, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 3, col 0 (empty row) => offset = 2 + 4 + 7 = 13 */
    inp = make_input(text, 3, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(13, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 2, col 5 => offset = 2 + 4 + 5 = 11 */
    inp = make_input(text, 2, 5);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(11, ev.cursor_offset_utf16);
    free_events(&ev);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * 4. No notification fires when terminal content is unchanged
 * ================================================================ */
TEST no_notification_on_unchanged_content(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    const char *text = "prompt> ls\nfile1.txt\nfile2.txt";
    WixenFrameA11yInput inp = make_input(text, 0, 10);
    WixenFrameA11yEvents ev;

    /* First frame: text_changed fires because prev_visible_text is NULL */
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT(ev.text_changed);
    free_events(&ev);

    /* Second frame: same text, same cursor => no text_changed */
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_FALSE(ev.text_changed);
    ASSERT_FALSE(ev.cursor_moved);
    ASSERT_FALSE(ev.should_announce_output);
    ASSERT_FALSE(ev.should_announce_line);
    free_events(&ev);

    /* Third frame: still identical */
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_FALSE(ev.text_changed);
    free_events(&ev);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * 5. Notification fires on semantic change (new output)
 * ================================================================ */
TEST notification_on_semantic_change(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    WixenFrameA11yEvents ev;

    /* Initial frame */
    WixenFrameA11yInput inp = make_input("prompt> ", 0, 8);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    free_events(&ev);

    /* New output appears (text changed) */
    WixenFrameA11yInput inp2 = make_input("prompt> ls\nfile1.txt\nfile2.txt", 2, 8);
    inp2.has_new_output = true;
    inp2.new_output_text = "file1.txt\nfile2.txt\n";
    wixen_frame_a11y_update(&fs, &inp2, &ev);

    ASSERT(ev.text_changed);
    ASSERT(ev.should_announce_output);
    ASSERT(ev.announce_text != NULL);
    free_events(&ev);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * 6. Large grid (200x50) cursor offset calculation completes without error
 * ================================================================ */
TEST large_grid_cursor_offset(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* Build a 200-column x 50-row grid */
    size_t cols = 200;
    size_t rows = 50;
    /* Each row: 200 chars + 1 newline (except last row) */
    size_t text_len = rows * cols + (rows - 1); /* newlines between rows */
    char *text = (char *)malloc(text_len + 1);
    ASSERT(text != NULL);

    size_t pos = 0;
    for (size_t r = 0; r < rows; r++) {
        for (size_t c = 0; c < cols; c++) {
            text[pos++] = 'A' + (char)(r % 26);
        }
        if (r < rows - 1) {
            text[pos++] = '\n';
        }
    }
    text[pos] = '\0';

    WixenFrameA11yEvents ev;

    /* Cursor at last row, last col */
    WixenFrameA11yInput inp = make_input(text, rows - 1, cols - 1);
    wixen_frame_a11y_update(&fs, &inp, &ev);

    /* Expected: (rows-1) * (cols+1) + (cols-1)
     * = 49 * 201 + 199 = 9849 + 199 = 10048
     * Wait, let me recalculate:
     * Each of the first 49 rows contributes 200 chars + 1 newline = 201
     * Then col 199 in the last row
     * offset = 49 * 201 + 199 = 9849 + 199 = 10048
     */
    int32_t expected = (int32_t)((rows - 1) * (cols + 1) + (cols - 1));
    ASSERT_EQ(expected, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Cursor at middle of grid */
    inp = make_input(text, 25, 100);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    expected = (int32_t)(25 * (cols + 1) + 100);
    ASSERT_EQ(expected, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 0, col 0 */
    inp = make_input(text, 0, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(0, ev.cursor_offset_utf16);
    free_events(&ev);

    free(text);
    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * 7. UTF-8 multibyte content doesn't break cursor offset
 * ================================================================ */
TEST utf8_multibyte_cursor_offset(void) {
    WixenFrameA11yState fs;
    wixen_frame_a11y_init(&fs, 100);

    /* The cursor offset in frame_update.c counts bytes (not codepoints).
     * UTF-8 multibyte chars take more bytes. Verify the offset calculation
     * walks bytes correctly when there are multibyte sequences.
     *
     * "caf\xc3\xa9" = "cafe" with e-acute (2-byte UTF-8)
     * Row 0: "caf\xc3\xa9" = 5 bytes
     * Row 1: "ok"          = 2 bytes
     *
     * "caf\xc3\xa9\nok"
     */
    const char *text = "caf\xc3\xa9\nok";

    WixenFrameA11yEvents ev;

    /* Row 0, col 0 => offset 0 */
    WixenFrameA11yInput inp = make_input(text, 0, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(0, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 0, col 3 => offset 3 (before the multibyte char) */
    inp = make_input(text, 0, 3);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(3, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 1, col 0 => offset past "caf\xc3\xa9\n" = 6 bytes
     * The frame_update cursor offset loop counts bytes as it walks.
     * "caf" = 3 bytes, then \xc3\xa9 = 2 bytes, then \n = 1 byte => 6.
     * But the loop at lines 158-163 increments offset for each byte p++,
     * and the col loop at 165-167 does p++ per col step.
     *
     * Actually, examining the code more carefully:
     * Row walk: skips until r < row, incrementing p and offset for each byte.
     * For row 0 -> row 1: walks "caf\xc3\xa9\n" counting bytes.
     *   c(offset=1), a(2), f(3), \xc3(4), \xa9(5), \n(r becomes 1, offset=6)
     * So row 1, col 0 => offset = 6. Correct!
     */
    inp = make_input(text, 1, 0);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(6, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Row 1, col 1 => offset = 6 + 1 = 7 */
    inp = make_input(text, 1, 1);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(7, ev.cursor_offset_utf16);
    free_events(&ev);

    /* CJK: 3-byte UTF-8. "\xe4\xb8\x96" = U+4E16 */
    const char *cjk_text = "A\xe4\xb8\x96" "B\nCD";
    /* Row 0: A(1) + \xe4\xb8\x96(3 bytes) + B(1) = 5 bytes + \n = 6
     * Row 1: col 1 => offset = 6 + 1 = 7
     */
    inp = make_input(cjk_text, 1, 1);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(7, ev.cursor_offset_utf16);
    free_events(&ev);

    /* Verify row 0, col 4 in CJK text => offset 4
     * The col loop counts 'col' steps, each is p++ (one byte per step).
     * col=0: A -> p++, col=1: \xe4 -> p++, col=2: \xb8 -> p++, col=3: \x96 -> p++
     * So col 4 => offset 4 (this is byte-level, not codepoint-level).
     */
    inp = make_input(cjk_text, 0, 4);
    wixen_frame_a11y_update(&fs, &inp, &ev);
    ASSERT_EQ(4, ev.cursor_offset_utf16);
    free_events(&ev);

    wixen_frame_a11y_free(&fs);
    PASS();
}

/* ================================================================
 * Also verify wixen_text_rowcol_to_offset consistency
 * ================================================================ */
TEST text_boundaries_rowcol_consistency(void) {
    /* Verify the text_boundaries helper agrees on row/col mapping */
    const char *text = "line0\nline1\nline2";
    size_t len = strlen(text);

    /* Row 0, col 0 => 0 */
    ASSERT_EQ(0, (int)wixen_text_rowcol_to_offset(text, len, 0, 0));

    /* Row 0, col 3 => 3 */
    ASSERT_EQ(3, (int)wixen_text_rowcol_to_offset(text, len, 0, 3));

    /* Row 1, col 0 => 6 (past "line0\n") */
    ASSERT_EQ(6, (int)wixen_text_rowcol_to_offset(text, len, 1, 0));

    /* Row 1, col 2 => 8 */
    ASSERT_EQ(8, (int)wixen_text_rowcol_to_offset(text, len, 1, 2));

    /* Row 2, col 0 => 12 (past "line0\nline1\n") */
    ASSERT_EQ(12, (int)wixen_text_rowcol_to_offset(text, len, 2, 0));

    PASS();
}

/* ================================================================
 * Suites
 * ================================================================ */

SUITE(a11y_perf_contract) {
    RUN_TEST(cursor_offset_row0_equals_col);
    RUN_TEST(cursor_offset_rowN_includes_previous_rows);
    RUN_TEST(cursor_offset_varying_row_lengths);
    RUN_TEST(no_notification_on_unchanged_content);
    RUN_TEST(notification_on_semantic_change);
    RUN_TEST(large_grid_cursor_offset);
    RUN_TEST(utf8_multibyte_cursor_offset);
    RUN_TEST(text_boundaries_rowcol_consistency);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(a11y_perf_contract);
    GREATEST_MAIN_END();
}
