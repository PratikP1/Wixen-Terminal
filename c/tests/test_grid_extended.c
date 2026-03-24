/* test_grid_extended.c — Additional grid tests for C-specific edge cases */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/grid.h"

TEST grid_scroll_region_down(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 4);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[1].cells[0], "B");
    wixen_cell_set_content(&g.rows[2].cells[0], "C");
    wixen_cell_set_content(&g.rows[3].cells[0], "D");
    wixen_grid_scroll_region_down(&g, 1, 3, 1);
    char buf[64];
    wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT_STR_EQ("A", buf); /* Unchanged */
    wixen_row_text(&g.rows[1], buf, sizeof(buf));
    ASSERT_STR_EQ("", buf);  /* New blank */
    wixen_row_text(&g.rows[2], buf, sizeof(buf));
    ASSERT_STR_EQ("B", buf); /* Was row 1 */
    wixen_row_text(&g.rows[3], buf, sizeof(buf));
    ASSERT_STR_EQ("D", buf); /* Unchanged, C scrolled out */
    wixen_grid_free(&g);
    PASS();
}

TEST grid_erase_in_display_mode3(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "X");
    wixen_grid_erase_in_display(&g, 3); /* Clear display + scrollback */
    char buf[64];
    wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT_STR_EQ("", buf);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_insert_at_end(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    /* Insert 2 blanks at the end — should not crash */
    wixen_grid_insert_blank_cells(&g, 4, 0, 2);
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_delete_more_than_exists(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_grid_delete_cells(&g, 0, 0, 100); /* Delete way more than cols */
    /* Should not crash */
    wixen_grid_free(&g);
    PASS();
}

TEST grid_visible_text_all_spaces(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 2);
    char buf[256];
    wixen_grid_visible_text(&g, buf, sizeof(buf));
    /* All spaces should produce empty lines */
    ASSERT_STR_EQ("\n", buf);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_write_char_wide_at_last_col(void) {
    WixenGrid g;
    wixen_grid_init(&g, 4, 2);
    g.cursor.col = 3; /* Last column */
    /* Writing a wide char at last col should wrap */
    wixen_grid_write_char(&g, "\xe4\xb8\xad", 2);
    /* Wide char can't fit at col 3 (only 1 col left), should wrap to next row */
    wixen_grid_free(&g);
    PASS();
}

TEST grid_row_clone_preserves_content(void) {
    WixenRow src, dst;
    wixen_row_init(&src, 5);
    wixen_cell_set_content(&src.cells[0], "H");
    wixen_cell_set_content(&src.cells[1], "i");
    src.cells[0].attrs.bold = true;
    wixen_row_clone(&dst, &src);
    ASSERT_STR_EQ("H", dst.cells[0].content);
    ASSERT(dst.cells[0].attrs.bold);
    /* Modify src, verify dst independent */
    wixen_cell_set_content(&src.cells[0], "Z");
    ASSERT_STR_EQ("H", dst.cells[0].content);
    wixen_row_free(&src);
    wixen_row_free(&dst);
    PASS();
}

TEST grid_resize_to_1x1(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    wixen_grid_resize(&g, 1, 1);
    ASSERT_EQ(1, (int)g.cols);
    ASSERT_EQ(1, (int)g.num_rows);
    ASSERT_EQ(0, (int)g.cursor.col);
    ASSERT_EQ(0, (int)g.cursor.row);
    wixen_grid_free(&g);
    PASS();
}

SUITE(grid_extended) {
    RUN_TEST(grid_scroll_region_down);
    RUN_TEST(grid_erase_in_display_mode3);
    RUN_TEST(grid_insert_at_end);
    RUN_TEST(grid_delete_more_than_exists);
    RUN_TEST(grid_visible_text_all_spaces);
    RUN_TEST(grid_write_char_wide_at_last_col);
    RUN_TEST(grid_row_clone_preserves_content);
    RUN_TEST(grid_resize_to_1x1);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(grid_extended);
    GREATEST_MAIN_END();
}
