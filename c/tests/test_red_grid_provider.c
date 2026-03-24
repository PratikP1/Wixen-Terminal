/* test_red_grid_provider.c — RED tests for IGridProvider a11y */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/grid.h"

/* These test the grid data that IGridProvider would expose */

TEST red_grid_dimensions(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    ASSERT_EQ(80, (int)g.cols);
    ASSERT_EQ(24, (int)g.num_rows);
    wixen_grid_free(&g);
    PASS();
}

TEST red_grid_cell_access(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    wixen_cell_set_content(&g.rows[2].cells[5], "X");
    WixenCell *c = wixen_grid_cell(&g, 5, 2);
    ASSERT(c != NULL);
    ASSERT_STR_EQ("X", c->content);
    wixen_grid_free(&g);
    PASS();
}

TEST red_grid_cell_out_of_bounds(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    WixenCell *c = wixen_grid_cell(&g, 99, 99);
    ASSERT(c == NULL);
    wixen_grid_free(&g);
    PASS();
}

TEST red_grid_row_col_count(void) {
    WixenGrid g;
    wixen_grid_init(&g, 40, 10);
    /* IGridProvider.RowCount and ColumnCount */
    ASSERT_EQ(10, (int)g.num_rows);
    ASSERT_EQ(40, (int)g.cols);
    wixen_grid_free(&g);
    PASS();
}

SUITE(red_grid_provider) {
    RUN_TEST(red_grid_dimensions);
    RUN_TEST(red_grid_cell_access);
    RUN_TEST(red_grid_cell_out_of_bounds);
    RUN_TEST(red_grid_row_col_count);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_grid_provider);
    GREATEST_MAIN_END();
}
