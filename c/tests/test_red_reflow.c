/* test_red_reflow.c — RED tests for grid reflow edge cases */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/grid.h"

TEST red_reflow_shrink_wide_char(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 3);
    /* Place a wide char (width=2) at col 8-9 */
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[8], "\xe4\xb8\xad"); /* 中 */
    g.rows[0].cells[8].width = 2;
    g.rows[0].cells[9].width = 0; /* continuation */
    /* Shrink to 9 cols — wide char at 8 can't fit (needs 2 cols, only 1 available) */
    wixen_grid_resize_with_reflow(&g, 9, 3);
    /* Should not crash. Wide char may be moved to next line or truncated. */
    ASSERT_EQ(9, (int)g.cols);
    wixen_grid_free(&g);
    PASS();
}

TEST red_reflow_grow_unwraps_fully(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 4);
    /* "ABCDEF" wrapped across 2 rows of 3 cols */
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[0].cells[2], "C");
    g.rows[0].wrapped = true;
    wixen_cell_set_content(&g.rows[1].cells[0], "D");
    wixen_cell_set_content(&g.rows[1].cells[1], "E");
    wixen_cell_set_content(&g.rows[1].cells[2], "F");
    /* Grow to 10 cols — should unwrap into a single row */
    wixen_grid_resize_with_reflow(&g, 10, 4);
    ASSERT_EQ(10, (int)g.cols);
    /* Row 0 should have ABCDEF */
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    ASSERT_STR_EQ("D", g.rows[0].cells[3].content);
    ASSERT_STR_EQ("F", g.rows[0].cells[5].content);
    ASSERT_FALSE(g.rows[0].wrapped);
    wixen_grid_free(&g);
    PASS();
}

TEST red_reflow_preserve_non_wrapped(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 3);
    /* Two separate lines (not wrapped) */
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    g.rows[0].wrapped = false;
    wixen_cell_set_content(&g.rows[1].cells[0], "B");
    g.rows[1].wrapped = false;
    /* Shrink to 5 cols */
    wixen_grid_resize_with_reflow(&g, 5, 3);
    /* Lines should remain separate */
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", g.rows[1].cells[0].content);
    wixen_grid_free(&g);
    PASS();
}

TEST red_reflow_empty_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    wixen_grid_resize_with_reflow(&g, 20, 3);
    ASSERT_EQ(20, (int)g.cols);
    ASSERT_EQ(3, (int)g.num_rows);
    wixen_grid_free(&g);
    PASS();
}

TEST red_reflow_same_size_noop(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    wixen_cell_set_content(&g.rows[0].cells[0], "X");
    wixen_grid_resize_with_reflow(&g, 10, 5);
    ASSERT_STR_EQ("X", g.rows[0].cells[0].content);
    wixen_grid_free(&g);
    PASS();
}

TEST red_reflow_many_wrapped_lines(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 10);
    /* Fill 9 cells across 3 rows, all wrapped */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++)
            wixen_cell_set_content(&g.rows[r].cells[c], "X");
        if (r < 2) g.rows[r].wrapped = true;
    }
    /* Grow to 9 cols — should unwrap into 1 row of 9 X's */
    wixen_grid_resize_with_reflow(&g, 9, 10);
    size_t x_count = 0;
    for (size_t c = 0; c < 9; c++) {
        if (g.rows[0].cells[c].content[0] == 'X') x_count++;
    }
    ASSERT_EQ(9, (int)x_count);
    wixen_grid_free(&g);
    PASS();
}

SUITE(red_reflow) {
    RUN_TEST(red_reflow_shrink_wide_char);
    RUN_TEST(red_reflow_grow_unwraps_fully);
    RUN_TEST(red_reflow_preserve_non_wrapped);
    RUN_TEST(red_reflow_empty_grid);
    RUN_TEST(red_reflow_same_size_noop);
    RUN_TEST(red_reflow_many_wrapped_lines);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_reflow);
    GREATEST_MAIN_END();
}
