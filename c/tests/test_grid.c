/* test_grid.c — Tests for WixenGrid, WixenRow */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/grid.h"

/* Helper: extract row text into static buffer */
static const char *row_text(const WixenGrid *g, size_t row) {
    static char buf[4096];
    if (row >= g->num_rows) return "";
    wixen_row_text(&g->rows[row], buf, sizeof(buf));
    return buf;
}

/* === Row Tests === */

TEST row_init_has_correct_count(void) {
    WixenRow row;
    wixen_row_init(&row, 80);
    ASSERT_EQ(80, (int)row.count);
    ASSERT_FALSE(row.wrapped);
    ASSERT_STR_EQ(" ", row.cells[0].content);
    ASSERT_STR_EQ(" ", row.cells[79].content);
    wixen_row_free(&row);
    PASS();
}

TEST row_clear_resets_all_cells(void) {
    WixenRow row;
    wixen_row_init(&row, 5);
    wixen_cell_set_content(&row.cells[0], "X");
    row.cells[0].attrs.bold = true;
    row.wrapped = true;
    wixen_row_clear(&row);
    ASSERT_STR_EQ(" ", row.cells[0].content);
    ASSERT_FALSE(row.cells[0].attrs.bold);
    ASSERT_FALSE(row.wrapped);
    wixen_row_free(&row);
    PASS();
}

TEST row_text_trims_trailing_spaces(void) {
    WixenRow row;
    wixen_row_init(&row, 10);
    wixen_cell_set_content(&row.cells[0], "H");
    wixen_cell_set_content(&row.cells[1], "i");
    char buf[64];
    wixen_row_text(&row, buf, sizeof(buf));
    ASSERT_STR_EQ("Hi", buf);
    wixen_row_free(&row);
    PASS();
}

TEST row_text_empty_row(void) {
    WixenRow row;
    wixen_row_init(&row, 10);
    char buf[64];
    wixen_row_text(&row, buf, sizeof(buf));
    ASSERT_STR_EQ("", buf);
    wixen_row_free(&row);
    PASS();
}

TEST row_clone_is_deep(void) {
    WixenRow src, dst;
    wixen_row_init(&src, 3);
    wixen_cell_set_content(&src.cells[0], "A");
    src.wrapped = true;
    wixen_row_clone(&dst, &src);
    ASSERT_EQ(3, (int)dst.count);
    ASSERT_STR_EQ("A", dst.cells[0].content);
    ASSERT(dst.wrapped);
    /* Mutating src doesn't affect dst */
    wixen_cell_set_content(&src.cells[0], "B");
    ASSERT_STR_EQ("A", dst.cells[0].content);
    wixen_row_free(&src);
    wixen_row_free(&dst);
    PASS();
}

/* === Grid Init/Free Tests === */

TEST grid_init_dimensions(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    ASSERT_EQ(80, (int)g.cols);
    ASSERT_EQ(24, (int)g.num_rows);
    ASSERT_EQ(0, (int)g.cursor.col);
    ASSERT_EQ(0, (int)g.cursor.row);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_init_cells_are_spaces(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    for (size_t r = 0; r < 3; r++) {
        for (size_t c = 0; c < 5; c++) {
            const WixenCell *cell = wixen_grid_cell_const(&g, c, r);
            ASSERT_STR_EQ(" ", cell->content);
        }
    }
    wixen_grid_free(&g);
    PASS();
}

TEST grid_cell_out_of_bounds_returns_null(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    ASSERT_EQ(NULL, wixen_grid_cell(&g, 5, 0));
    ASSERT_EQ(NULL, wixen_grid_cell(&g, 0, 3));
    ASSERT_EQ(NULL, wixen_grid_cell(&g, 100, 100));
    wixen_grid_free(&g);
    PASS();
}

/* === Write Char Tests === */

TEST grid_write_char_basic(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    wixen_grid_write_char(&g, "A", 1);
    ASSERT_STR_EQ("A", row_text(&g, 0));
    ASSERT_EQ(1, (int)g.cursor.col);
    ASSERT_EQ(0, (int)g.cursor.row);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_write_char_sequence(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    wixen_grid_write_char(&g, "H", 1);
    wixen_grid_write_char(&g, "e", 1);
    wixen_grid_write_char(&g, "l", 1);
    wixen_grid_write_char(&g, "l", 1);
    wixen_grid_write_char(&g, "o", 1);
    ASSERT_STR_EQ("Hello", row_text(&g, 0));
    ASSERT_EQ(5, (int)g.cursor.col);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_write_char_wraps_at_right_margin(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 3);
    wixen_grid_write_char(&g, "A", 1);
    wixen_grid_write_char(&g, "B", 1);
    wixen_grid_write_char(&g, "C", 1);
    /* Cursor is now at col 3 (past right margin) */
    wixen_grid_write_char(&g, "D", 1);
    /* Should have wrapped to next line */
    ASSERT_STR_EQ("ABC", row_text(&g, 0));
    ASSERT_STR_EQ("D", row_text(&g, 1));
    ASSERT(g.rows[0].wrapped);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_write_char_scrolls_at_bottom(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 2);
    /* Fill both rows */
    wixen_grid_write_char(&g, "A", 1);
    wixen_grid_write_char(&g, "B", 1);
    wixen_grid_write_char(&g, "C", 1);
    wixen_grid_write_char(&g, "D", 1);
    wixen_grid_write_char(&g, "E", 1);
    wixen_grid_write_char(&g, "F", 1);
    /* Next char should scroll */
    wixen_grid_write_char(&g, "G", 1);
    /* Row 0 should now be "DEF" (old row 1), row 1 should have "G" */
    ASSERT_STR_EQ("DEF", row_text(&g, 0));
    ASSERT_STR_EQ("G", row_text(&g, 1));
    wixen_grid_free(&g);
    PASS();
}

TEST grid_write_wide_char(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 1);
    wixen_grid_write_char(&g, "\xe4\xb8\xad", 2); /* 中 (wide) */
    ASSERT_EQ(2, (int)g.cursor.col);
    const WixenCell *c0 = wixen_grid_cell_const(&g, 0, 0);
    const WixenCell *c1 = wixen_grid_cell_const(&g, 1, 0);
    ASSERT_STR_EQ("\xe4\xb8\xad", c0->content);
    ASSERT_EQ(2, c0->width);
    ASSERT_STR_EQ("", c1->content);
    ASSERT_EQ(0, c1->width);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_write_char_applies_current_attrs(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 1);
    g.current_attrs.bold = true;
    g.current_attrs.fg = wixen_color_indexed(1);
    wixen_grid_write_char(&g, "X", 1);
    const WixenCell *c = wixen_grid_cell_const(&g, 0, 0);
    ASSERT(c->attrs.bold);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.fg.type);
    ASSERT_EQ(1, c->attrs.fg.index);
    wixen_grid_free(&g);
    PASS();
}

/* === Scroll Tests === */

TEST grid_scroll_up_shifts_rows(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[1].cells[0], "B");
    wixen_cell_set_content(&g.rows[2].cells[0], "C");
    wixen_grid_scroll_up(&g, 1);
    ASSERT_STR_EQ("B", row_text(&g, 0));
    ASSERT_STR_EQ("C", row_text(&g, 1));
    ASSERT_STR_EQ("", row_text(&g, 2)); /* New blank row */
    wixen_grid_free(&g);
    PASS();
}

TEST grid_scroll_down_shifts_rows(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[1].cells[0], "B");
    wixen_cell_set_content(&g.rows[2].cells[0], "C");
    wixen_grid_scroll_down(&g, 1);
    ASSERT_STR_EQ("", row_text(&g, 0)); /* New blank row */
    ASSERT_STR_EQ("A", row_text(&g, 1));
    ASSERT_STR_EQ("B", row_text(&g, 2));
    /* C was scrolled out */
    wixen_grid_free(&g);
    PASS();
}

TEST grid_scroll_region_up(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 5);
    for (int i = 0; i < 5; i++) {
        char ch[2] = { 'A' + (char)i, 0 };
        wixen_cell_set_content(&g.rows[i].cells[0], ch);
    }
    /* Scroll rows 1-3 up by 1 (row 1 scrolled out, row 4 blank) */
    wixen_grid_scroll_region_up(&g, 1, 4, 1);
    ASSERT_STR_EQ("A", row_text(&g, 0)); /* Unchanged */
    ASSERT_STR_EQ("C", row_text(&g, 1)); /* Was row 2 */
    ASSERT_STR_EQ("D", row_text(&g, 2)); /* Was row 3 */
    ASSERT_STR_EQ("", row_text(&g, 3));  /* New blank */
    ASSERT_STR_EQ("E", row_text(&g, 4)); /* Unchanged */
    wixen_grid_free(&g);
    PASS();
}

/* === Erase Tests === */

TEST grid_erase_in_line_cursor_to_end(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    for (int i = 0; i < 5; i++) {
        char ch[2] = { 'A' + (char)i, 0 };
        wixen_cell_set_content(&g.rows[0].cells[i], ch);
    }
    g.cursor.col = 2;
    wixen_grid_erase_in_line(&g, 0, 0); /* Erase from cursor to end */
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", g.rows[0].cells[1].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[2].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[3].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[4].content);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_erase_in_line_start_to_cursor(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    for (int i = 0; i < 5; i++) {
        char ch[2] = { 'A' + (char)i, 0 };
        wixen_cell_set_content(&g.rows[0].cells[i], ch);
    }
    g.cursor.col = 2;
    wixen_grid_erase_in_line(&g, 0, 1); /* Erase start to cursor */
    ASSERT_STR_EQ(" ", g.rows[0].cells[0].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[1].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[2].content);
    ASSERT_STR_EQ("D", g.rows[0].cells[3].content);
    ASSERT_STR_EQ("E", g.rows[0].cells[4].content);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_erase_in_line_entire(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "X");
    wixen_grid_erase_in_line(&g, 0, 2);
    ASSERT_STR_EQ("", row_text(&g, 0));
    wixen_grid_free(&g);
    PASS();
}

TEST grid_erase_in_display_cursor_to_end(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 3);
    for (size_t r = 0; r < 3; r++)
        wixen_cell_set_content(&g.rows[r].cells[0], "X");
    g.cursor.row = 1;
    g.cursor.col = 0;
    wixen_grid_erase_in_display(&g, 0);
    ASSERT_STR_EQ("X", row_text(&g, 0)); /* Unchanged */
    ASSERT_STR_EQ("", row_text(&g, 1));  /* Erased from cursor */
    ASSERT_STR_EQ("", row_text(&g, 2));  /* Erased below */
    wixen_grid_free(&g);
    PASS();
}

/* === Resize Tests === */

TEST grid_resize_grow_cols(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 2);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_grid_resize(&g, 5, 2);
    ASSERT_EQ(5, (int)g.cols);
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[3].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[4].content);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_resize_shrink_cols(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 2);
    wixen_cell_set_content(&g.rows[0].cells[4], "Z");
    wixen_grid_resize(&g, 3, 2);
    ASSERT_EQ(3, (int)g.cols);
    /* Cell at index 4 is gone */
    ASSERT_EQ(NULL, wixen_grid_cell(&g, 4, 0));
    wixen_grid_free(&g);
    PASS();
}

TEST grid_resize_grow_rows(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 2);
    wixen_grid_resize(&g, 3, 4);
    ASSERT_EQ(4, (int)g.num_rows);
    ASSERT_STR_EQ(" ", g.rows[3].cells[0].content); /* New row is blank */
    wixen_grid_free(&g);
    PASS();
}

TEST grid_resize_shrink_rows(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 4);
    wixen_cell_set_content(&g.rows[3].cells[0], "Z");
    wixen_grid_resize(&g, 3, 2);
    ASSERT_EQ(2, (int)g.num_rows);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_resize_clamps_cursor(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 10);
    g.cursor.col = 8;
    g.cursor.row = 8;
    wixen_grid_resize(&g, 5, 5);
    ASSERT_EQ(4, (int)g.cursor.col);
    ASSERT_EQ(4, (int)g.cursor.row);
    wixen_grid_free(&g);
    PASS();
}

/* === Insert/Delete Cell Tests === */

TEST grid_insert_blank_cells(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[0].cells[2], "C");
    wixen_grid_insert_blank_cells(&g, 1, 0, 2);
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[1].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[2].content);
    ASSERT_STR_EQ("B", g.rows[0].cells[3].content);
    ASSERT_STR_EQ("C", g.rows[0].cells[4].content);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_delete_cells(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    const char *text = "ABCDE";
    for (int i = 0; i < 5; i++) {
        char ch[2] = { text[i], 0 };
        wixen_cell_set_content(&g.rows[0].cells[i], ch);
    }
    wixen_grid_delete_cells(&g, 1, 0, 2);
    ASSERT_STR_EQ("A", g.rows[0].cells[0].content);
    ASSERT_STR_EQ("D", g.rows[0].cells[1].content);
    ASSERT_STR_EQ("E", g.rows[0].cells[2].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[3].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[4].content);
    wixen_grid_free(&g);
    PASS();
}

/* === Visible Text === */

TEST grid_visible_text(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "H");
    wixen_cell_set_content(&g.rows[0].cells[1], "i");
    wixen_cell_set_content(&g.rows[2].cells[0], "!");
    char buf[256];
    wixen_grid_visible_text(&g, buf, sizeof(buf));
    ASSERT_STR_EQ("Hi\n\n!", buf);
    wixen_grid_free(&g);
    PASS();
}

/* === Edge Cases (C-specific) === */

TEST grid_init_zero_cols(void) {
    /* Edge case: 0-width grid should not crash */
    WixenGrid g;
    wixen_grid_init(&g, 0, 5);
    /* Write should be no-op */
    wixen_grid_write_char(&g, "A", 1);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_scroll_more_than_size(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_grid_scroll_up(&g, 100); /* More than num_rows */
    /* All rows should be blank */
    ASSERT_STR_EQ("", row_text(&g, 0));
    ASSERT_STR_EQ("", row_text(&g, 1));
    ASSERT_STR_EQ("", row_text(&g, 2));
    wixen_grid_free(&g);
    PASS();
}

TEST grid_erase_with_cursor_at_end(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 1);
    wixen_cell_set_content(&g.rows[0].cells[0], "X");
    g.cursor.col = 4; /* Last column */
    wixen_grid_erase_in_line(&g, 0, 0);
    ASSERT_STR_EQ("X", g.rows[0].cells[0].content);
    ASSERT_STR_EQ(" ", g.rows[0].cells[4].content);
    wixen_grid_free(&g);
    PASS();
}

/* === Suites === */

SUITE(row_tests) {
    RUN_TEST(row_init_has_correct_count);
    RUN_TEST(row_clear_resets_all_cells);
    RUN_TEST(row_text_trims_trailing_spaces);
    RUN_TEST(row_text_empty_row);
    RUN_TEST(row_clone_is_deep);
}

SUITE(grid_init_tests) {
    RUN_TEST(grid_init_dimensions);
    RUN_TEST(grid_init_cells_are_spaces);
    RUN_TEST(grid_cell_out_of_bounds_returns_null);
}

SUITE(grid_write_tests) {
    RUN_TEST(grid_write_char_basic);
    RUN_TEST(grid_write_char_sequence);
    RUN_TEST(grid_write_char_wraps_at_right_margin);
    RUN_TEST(grid_write_char_scrolls_at_bottom);
    RUN_TEST(grid_write_wide_char);
    RUN_TEST(grid_write_char_applies_current_attrs);
}

SUITE(grid_scroll_tests) {
    RUN_TEST(grid_scroll_up_shifts_rows);
    RUN_TEST(grid_scroll_down_shifts_rows);
    RUN_TEST(grid_scroll_region_up);
}

SUITE(grid_erase_tests) {
    RUN_TEST(grid_erase_in_line_cursor_to_end);
    RUN_TEST(grid_erase_in_line_start_to_cursor);
    RUN_TEST(grid_erase_in_line_entire);
    RUN_TEST(grid_erase_in_display_cursor_to_end);
}

SUITE(grid_resize_tests) {
    RUN_TEST(grid_resize_grow_cols);
    RUN_TEST(grid_resize_shrink_cols);
    RUN_TEST(grid_resize_grow_rows);
    RUN_TEST(grid_resize_shrink_rows);
    RUN_TEST(grid_resize_clamps_cursor);
}

SUITE(grid_cell_ops_tests) {
    RUN_TEST(grid_insert_blank_cells);
    RUN_TEST(grid_delete_cells);
}

SUITE(grid_text_tests) {
    RUN_TEST(grid_visible_text);
}

/* === Reflow === */

TEST grid_reflow_shrink_joins_wrapped(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    /* Write "ABCDE" which fills row 0, then "FG" wraps to row 1 */
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[0].cells[2], "C");
    wixen_cell_set_content(&g.rows[0].cells[3], "D");
    wixen_cell_set_content(&g.rows[0].cells[4], "E");
    g.rows[0].wrapped = true;
    wixen_cell_set_content(&g.rows[1].cells[0], "F");
    wixen_cell_set_content(&g.rows[1].cells[1], "G");

    /* Reflow to 3 cols — ABCDEFG should become ABC/DEF/G */
    wixen_grid_resize_with_reflow(&g, 3, 4);
    ASSERT_EQ(3, (int)g.cols);
    char buf[64];
    wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT_STR_EQ("ABC", buf);
    wixen_row_text(&g.rows[1], buf, sizeof(buf));
    ASSERT_STR_EQ("DEF", buf);
    wixen_row_text(&g.rows[2], buf, sizeof(buf));
    ASSERT_STR_EQ("G", buf);
    wixen_grid_free(&g);
    PASS();
}

TEST grid_reflow_grow_unwraps(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 4);
    /* ABC wrapped, DEF wrapped, G on row 2 */
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[0].cells[2], "C");
    g.rows[0].wrapped = true;
    wixen_cell_set_content(&g.rows[1].cells[0], "D");
    wixen_cell_set_content(&g.rows[1].cells[1], "E");
    wixen_cell_set_content(&g.rows[1].cells[2], "F");
    g.rows[1].wrapped = true;
    wixen_cell_set_content(&g.rows[2].cells[0], "G");

    /* Reflow to 10 cols — ABCDEFG fits on one row */
    wixen_grid_resize_with_reflow(&g, 10, 3);
    char buf[64];
    wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT_STR_EQ("ABCDEFG", buf);
    wixen_row_text(&g.rows[1], buf, sizeof(buf));
    ASSERT_STR_EQ("", buf); /* Empty */
    wixen_grid_free(&g);
    PASS();
}

SUITE(grid_reflow_tests) {
    RUN_TEST(grid_reflow_shrink_joins_wrapped);
    RUN_TEST(grid_reflow_grow_unwraps);
}

SUITE(grid_edge_cases) {
    RUN_TEST(grid_init_zero_cols);
    RUN_TEST(grid_scroll_more_than_size);
    RUN_TEST(grid_erase_with_cursor_at_end);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(row_tests);
    RUN_SUITE(grid_init_tests);
    RUN_SUITE(grid_write_tests);
    RUN_SUITE(grid_scroll_tests);
    RUN_SUITE(grid_erase_tests);
    RUN_SUITE(grid_resize_tests);
    RUN_SUITE(grid_cell_ops_tests);
    RUN_SUITE(grid_text_tests);
    RUN_SUITE(grid_reflow_tests);
    RUN_SUITE(grid_edge_cases);
    GREATEST_MAIN_END();
}
