/* test_selection_extended.c — Additional selection edge cases */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/selection.h"

TEST sel_word_type(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 3, WIXEN_SEL_WORD);
    ASSERT_EQ(WIXEN_SEL_WORD, sel.type);
    PASS();
}

TEST sel_line_full_row(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 5, WIXEN_SEL_LINE);
    wixen_selection_update(&sel, 79, 5);
    /* Line selection: entire row 5 selected regardless of col */
    ASSERT(wixen_selection_contains(&sel, 0, 5, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 4, 80));
    PASS();
}

TEST sel_block_same_col(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 2, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 5, 8);
    /* Single-column block: only col 5, rows 2-8 */
    ASSERT(wixen_selection_contains(&sel, 5, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 5, 5, 80));
    ASSERT(wixen_selection_contains(&sel, 5, 8, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 6, 5, 80));
    PASS();
}

TEST sel_ordered_same_point(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 3, 3, WIXEN_SEL_NORMAL);
    WixenGridPoint start, end;
    wixen_selection_ordered(&sel, &start, &end);
    ASSERT_EQ(3, (int)start.col);
    ASSERT_EQ(3, (int)start.row);
    ASSERT_EQ(3, (int)end.col);
    ASSERT_EQ(3, (int)end.row);
    PASS();
}

TEST sel_row_count_single(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 5, WIXEN_SEL_NORMAL);
    ASSERT_EQ(1, (int)wixen_selection_row_count(&sel));
    PASS();
}

TEST sel_clear_then_contains_false(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 79, 23);
    ASSERT(wixen_selection_contains(&sel, 40, 12, 80));
    wixen_selection_clear(&sel);
    ASSERT_FALSE(wixen_selection_contains(&sel, 40, 12, 80));
    PASS();
}

TEST sel_update_shrink(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 79, 10);
    ASSERT_EQ(11, (int)wixen_selection_row_count(&sel));
    wixen_selection_update(&sel, 5, 2);
    ASSERT_EQ(3, (int)wixen_selection_row_count(&sel));
    PASS();
}

SUITE(selection_extended) {
    RUN_TEST(sel_word_type);
    RUN_TEST(sel_line_full_row);
    RUN_TEST(sel_block_same_col);
    RUN_TEST(sel_ordered_same_point);
    RUN_TEST(sel_row_count_single);
    RUN_TEST(sel_clear_then_contains_false);
    RUN_TEST(sel_update_shrink);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(selection_extended);
    GREATEST_MAIN_END();
}
