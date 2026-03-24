/* test_selection_text.c — Selection text extraction from grid */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/selection.h"
#include "wixen/core/grid.h"

TEST sel_extract_single_row(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "H");
    wixen_cell_set_content(&g.rows[0].cells[1], "e");
    wixen_cell_set_content(&g.rows[0].cells[2], "l");
    wixen_cell_set_content(&g.rows[0].cells[3], "l");
    wixen_cell_set_content(&g.rows[0].cells[4], "o");

    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 4, 0);

    /* Extract text manually */
    WixenGridPoint start, end;
    wixen_selection_ordered(&sel, &start, &end);
    char buf[64] = {0};
    size_t pos = 0;
    for (size_t c = start.col; c <= end.col && c < g.cols; c++) {
        const char *ch = g.rows[0].cells[c].content;
        if (ch && ch[0]) { size_t l = strlen(ch); memcpy(buf + pos, ch, l); pos += l; }
    }
    ASSERT_STR_EQ("Hello", buf);

    wixen_grid_free(&g);
    PASS();
}

TEST sel_extract_multi_row(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[1].cells[0], "C");
    wixen_cell_set_content(&g.rows[1].cells[1], "D");

    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 1, 1);

    ASSERT_EQ(2, (int)wixen_selection_row_count(&sel));
    wixen_grid_free(&g);
    PASS();
}

TEST sel_contains_middle(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 2, 2, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 8, 5);
    ASSERT(wixen_selection_contains(&sel, 5, 3, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 5, 1, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 5, 6, 80));
    PASS();
}

TEST sel_block_rectangle(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 2, 1, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 5, 4);
    /* Block selection: cols 2-5, rows 1-4 */
    ASSERT(wixen_selection_contains(&sel, 3, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 5, 4, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 1, 2, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 6, 2, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 3, 0, 80));
    PASS();
}

TEST sel_line_mode(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 2, WIXEN_SEL_LINE);
    wixen_selection_update(&sel, 3, 4);
    /* Line selection: all cols in rows 2-4 */
    ASSERT(wixen_selection_contains(&sel, 0, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 3, 80));
    ASSERT(wixen_selection_contains(&sel, 0, 4, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 1, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 5, 80));
    PASS();
}

TEST sel_active_flag(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    ASSERT_FALSE(sel.active);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    ASSERT(sel.active);
    wixen_selection_clear(&sel);
    ASSERT_FALSE(sel.active);
    PASS();
}

SUITE(selection_text) {
    RUN_TEST(sel_extract_single_row);
    RUN_TEST(sel_extract_multi_row);
    RUN_TEST(sel_contains_middle);
    RUN_TEST(sel_block_rectangle);
    RUN_TEST(sel_line_mode);
    RUN_TEST(sel_active_flag);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(selection_text);
    GREATEST_MAIN_END();
}
