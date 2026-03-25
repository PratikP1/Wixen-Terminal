/* test_red_copilot_p6_dynamic_buffers.c — RED tests for Copilot finding P6
 *
 * P6: Accessibility text is truncated by fixed buffers (8192, 512).
 * Text should use dynamic allocation to handle arbitrary terminal sizes.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/grid.h"

/* P6a: visible_text handles grid wider than 512 chars */
TEST red_p6_wide_grid_not_truncated(void) {
    WixenGrid grid;
    wixen_grid_init(&grid, 200, 2); /* 200 columns */
    /* Fill row 0 with 'A' */
    for (size_t c = 0; c < 200; c++) {
        wixen_cell_set_content(&grid.rows[0].cells[c], "A");
    }
    /* Extract visible text — should NOT truncate at 512 */
    char *text = wixen_grid_visible_text_dynamic(&grid);
    ASSERT(text != NULL);
    size_t len = strlen(text);
    /* Row of 200 'A's + newline + empty row + newline = at least 200 */
    ASSERT(len >= 200);
    /* Verify all A's present */
    int count_a = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == 'A') count_a++;
    }
    ASSERT_EQ(200, count_a);
    free(text);
    wixen_grid_free(&grid);
    PASS();
}

/* P6b: visible_text handles tall grid (more than 8192 total chars) */
TEST red_p6_tall_grid_not_truncated(void) {
    WixenGrid grid;
    wixen_grid_init(&grid, 80, 200); /* 200 rows × 80 cols */
    /* Fill every row with 'B' */
    for (size_t r = 0; r < 200; r++) {
        for (size_t c = 0; c < 80; c++) {
            wixen_cell_set_content(&grid.rows[r].cells[c], "B");
        }
    }
    char *text = wixen_grid_visible_text_dynamic(&grid);
    ASSERT(text != NULL);
    size_t len = strlen(text);
    /* 200 rows × (80 chars + newline) = 16200 chars — must exceed 8192 */
    ASSERT(len > 8192);
    int count_b = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == 'B') count_b++;
    }
    ASSERT_EQ(200 * 80, count_b);
    free(text);
    wixen_grid_free(&grid);
    PASS();
}

/* P6c: row_text_dynamic handles long lines */
TEST red_p6_long_row_not_truncated(void) {
    WixenGrid grid;
    wixen_grid_init(&grid, 500, 1);
    for (size_t c = 0; c < 500; c++) {
        wixen_cell_set_content(&grid.rows[0].cells[c], "X");
    }
    char *text = wixen_row_text_dynamic(&grid.rows[0]);
    ASSERT(text != NULL);
    ASSERT_EQ(500, (int)strlen(text));
    free(text);
    wixen_grid_free(&grid);
    PASS();
}

/* P6d: UTF-8 multi-byte cells don't overflow */
TEST red_p6_utf8_row_no_overflow(void) {
    WixenGrid grid;
    wixen_grid_init(&grid, 100, 1);
    /* Each cell has a 3-byte UTF-8 char (e.g., CJK) */
    for (size_t c = 0; c < 100; c++) {
        wixen_cell_set_content(&grid.rows[0].cells[c], "\xe4\xb8\xad"); /* 中 */
    }
    char *text = wixen_row_text_dynamic(&grid.rows[0]);
    ASSERT(text != NULL);
    /* 100 cells × 3 bytes = 300 bytes */
    ASSERT_EQ(300, (int)strlen(text));
    free(text);
    wixen_grid_free(&grid);
    PASS();
}

SUITE(red_copilot_p6_dynamic_buffers) {
    RUN_TEST(red_p6_wide_grid_not_truncated);
    RUN_TEST(red_p6_tall_grid_not_truncated);
    RUN_TEST(red_p6_long_row_not_truncated);
    RUN_TEST(red_p6_utf8_row_no_overflow);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_copilot_p6_dynamic_buffers);
    GREATEST_MAIN_END();
}
