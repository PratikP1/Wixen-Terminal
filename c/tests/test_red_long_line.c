/* test_red_long_line.c — RED tests for P1.5: long-line truncation fix.
 *
 * Verifies that:
 *   1. wixen_row_text_dynamic extracts full text from rows > 512 chars
 *   2. Line-change detection works on long lines (> 512 bytes)
 *   3. Cursor offset calculation is correct on rows with 800+ chars
 *   4. Comparison between two long lines detects actual differences
 *   5. Dynamic extraction matches fixed-buffer extraction for short lines
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/grid.h"

/* ---------- helpers ---------- */

/* Build a row with `count` ASCII characters ('A','B','C',...) cycling. */
static void fill_row_ascii(WixenRow *row, size_t count) {
    for (size_t i = 0; i < count && i < row->count; i++) {
        char ch[2] = { (char)('A' + (i % 26)), '\0' };
        wixen_cell_set_content(&row->cells[i], ch);
        row->cells[i].width = 1;
    }
}

/* Simulate the main.c cursor-offset loop using dynamic allocation. */
static int32_t compute_cursor_offset_dynamic(const WixenGrid *g, size_t cur_row, size_t cur_col) {
    int32_t utf16_off = 0;
    for (size_t r = 0; r < cur_row && r < g->num_rows; r++) {
        char *row_text = wixen_row_text_dynamic(&g->rows[r]);
        if (row_text) {
            utf16_off += (int32_t)strlen(row_text) + 1; /* +1 for newline */
            free(row_text);
        }
    }
    utf16_off += (int32_t)cur_col;
    return utf16_off;
}

/* Simulate the OLD (buggy) cursor-offset loop using 512-byte buffer. */
static int32_t compute_cursor_offset_old(const WixenGrid *g, size_t cur_row, size_t cur_col) {
    int32_t utf16_off = 0;
    for (size_t r = 0; r < cur_row && r < g->num_rows; r++) {
        char rbuf[512];
        size_t rlen = wixen_row_text(&g->rows[r], rbuf, sizeof(rbuf));
        utf16_off += (int32_t)rlen + 1;
    }
    utf16_off += (int32_t)cur_col;
    return utf16_off;
}

/* ====== TEST 1: Row with 1000 chars extracts fully ====== */

TEST dynamic_extract_1000_chars(void) {
    WixenRow row;
    wixen_row_init(&row, 1200);
    fill_row_ascii(&row, 1000);

    char *text = wixen_row_text_dynamic(&row);
    ASSERT(text != NULL);
    ASSERT_EQ(1000, (int)strlen(text));

    /* Verify first and last character */
    ASSERT_EQ('A', text[0]);
    ASSERT_EQ('A' + (999 % 26), text[999]);

    free(text);
    wixen_row_free(&row);
    PASS();
}

/* ====== TEST 2: Line-change detection works on lines > 512 bytes ====== */

TEST line_change_detection_long_lines(void) {
    /* Build two rows: identical up to position 600, then differ */
    WixenRow row_a, row_b;
    wixen_row_init(&row_a, 1024);
    wixen_row_init(&row_b, 1024);
    fill_row_ascii(&row_a, 800);
    fill_row_ascii(&row_b, 800);

    /* Make them differ at position 600 */
    wixen_cell_set_content(&row_b.cells[600], "Z");

    char *text_a = wixen_row_text_dynamic(&row_a);
    char *text_b = wixen_row_text_dynamic(&row_b);

    ASSERT(text_a != NULL);
    ASSERT(text_b != NULL);

    /* Dynamic comparison correctly detects the difference */
    ASSERT(strcmp(text_a, text_b) != 0);

    /* OLD 512-byte approach would see them as identical (both truncated at 511) */
    char buf_a[512], buf_b[512];
    wixen_row_text(&row_a, buf_a, sizeof(buf_a));
    wixen_row_text(&row_b, buf_b, sizeof(buf_b));
    /* With old buffers, both are truncated before position 600 — looks same */
    ASSERT(strcmp(buf_a, buf_b) == 0);

    free(text_a);
    free(text_b);
    wixen_row_free(&row_a);
    wixen_row_free(&row_b);
    PASS();
}

/* ====== TEST 3: Cursor offset is correct on row with 800+ chars ====== */

TEST cursor_offset_long_row(void) {
    WixenGrid g;
    wixen_grid_init(&g, 1024, 3);

    /* Row 0: 900 characters, Row 1: 100 characters */
    fill_row_ascii(&g.rows[0], 900);
    fill_row_ascii(&g.rows[1], 100);

    size_t cur_row = 2;
    size_t cur_col = 5;

    int32_t dyn_offset = compute_cursor_offset_dynamic(&g, cur_row, cur_col);
    int32_t old_offset = compute_cursor_offset_old(&g, cur_row, cur_col);

    /* Dynamic offset: 900 + 1 (newline) + 100 + 1 (newline) + 5 = 1007 */
    ASSERT_EQ(1007, dyn_offset);

    /* Old offset is wrong because row 0 was truncated to 511 chars */
    ASSERT(old_offset != dyn_offset);
    /* Old: 511 + 1 + 100 + 1 + 5 = 618 */
    ASSERT_EQ(618, old_offset);

    wixen_grid_free(&g);
    PASS();
}

/* ====== TEST 4: Comparison between two long lines detects actual differences ====== */

TEST long_line_diff_detection(void) {
    WixenRow row_old, row_new;
    wixen_row_init(&row_old, 1024);
    wixen_row_init(&row_new, 1024);

    /* Both start with same 700-char prefix */
    fill_row_ascii(&row_old, 700);
    fill_row_ascii(&row_new, 700);

    /* "new" has 50 more characters appended (simulate typing) */
    for (size_t i = 700; i < 750; i++) {
        char ch[2] = { 'X', '\0' };
        wixen_cell_set_content(&row_new.cells[i], ch);
        row_new.cells[i].width = 1;
    }

    char *text_old = wixen_row_text_dynamic(&row_old);
    char *text_new = wixen_row_text_dynamic(&row_new);

    ASSERT(text_old != NULL);
    ASSERT(text_new != NULL);

    size_t old_len = strlen(text_old);
    size_t new_len = strlen(text_new);
    ASSERT_EQ(700, (int)old_len);
    ASSERT_EQ(750, (int)new_len);

    /* Correctly detect this as an append (prefix match) */
    bool is_append = (new_len > old_len && memcmp(text_new, text_old, old_len) == 0);
    ASSERT(is_append);

    free(text_old);
    free(text_new);
    wixen_row_free(&row_old);
    wixen_row_free(&row_new);
    PASS();
}

/* ====== TEST 5: Dynamic matches fixed-buffer for short lines ====== */

TEST dynamic_matches_fixed_for_short_lines(void) {
    WixenRow row;
    wixen_row_init(&row, 80);
    fill_row_ascii(&row, 40);

    char fixed_buf[512];
    wixen_row_text(&row, fixed_buf, sizeof(fixed_buf));

    char *dyn_buf = wixen_row_text_dynamic(&row);
    ASSERT(dyn_buf != NULL);

    ASSERT_STR_EQ(fixed_buf, dyn_buf);

    free(dyn_buf);
    wixen_row_free(&row);
    PASS();
}

/* ====== Suites ====== */

SUITE(long_line_tests) {
    RUN_TEST(dynamic_extract_1000_chars);
    RUN_TEST(line_change_detection_long_lines);
    RUN_TEST(cursor_offset_long_row);
    RUN_TEST(long_line_diff_detection);
    RUN_TEST(dynamic_matches_fixed_for_short_lines);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(long_line_tests);
    GREATEST_MAIN_END();
}
