/* test_selection.c — Tests for WixenSelection */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/selection.h"

TEST selection_init_inactive(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    ASSERT_FALSE(sel.active);
    PASS();
}

TEST selection_start_activates(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 3, WIXEN_SEL_NORMAL);
    ASSERT(sel.active);
    ASSERT_EQ(5, (int)sel.anchor.col);
    ASSERT_EQ(3, (int)sel.anchor.row);
    ASSERT_EQ(5, (int)sel.end.col);
    ASSERT_EQ(3, (int)sel.end.row);
    ASSERT_EQ(WIXEN_SEL_NORMAL, sel.type);
    PASS();
}

TEST selection_update_moves_end(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 10, 5);
    ASSERT_EQ(0, (int)sel.anchor.col);
    ASSERT_EQ(0, (int)sel.anchor.row);
    ASSERT_EQ(10, (int)sel.end.col);
    ASSERT_EQ(5, (int)sel.end.row);
    PASS();
}

TEST selection_clear_deactivates(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    ASSERT(sel.active);
    wixen_selection_clear(&sel);
    ASSERT_FALSE(sel.active);
    PASS();
}

TEST selection_ordered_forward(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 2, 1, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 8, 3);
    WixenGridPoint start, end;
    wixen_selection_ordered(&sel, &start, &end);
    ASSERT_EQ(2, (int)start.col);
    ASSERT_EQ(1, (int)start.row);
    ASSERT_EQ(8, (int)end.col);
    ASSERT_EQ(3, (int)end.row);
    PASS();
}

TEST selection_ordered_backward(void) {
    /* Anchor after end — reversed */
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 8, 3, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 2, 1);
    WixenGridPoint start, end;
    wixen_selection_ordered(&sel, &start, &end);
    ASSERT_EQ(2, (int)start.col);
    ASSERT_EQ(1, (int)start.row);
    ASSERT_EQ(8, (int)end.col);
    ASSERT_EQ(3, (int)end.row);
    PASS();
}

TEST selection_contains_normal_single_row(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 3, 5, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 8, 5);
    ASSERT(wixen_selection_contains(&sel, 3, 5, 80));
    ASSERT(wixen_selection_contains(&sel, 5, 5, 80));
    ASSERT(wixen_selection_contains(&sel, 8, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 2, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 9, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 5, 4, 80));
    PASS();
}

TEST selection_contains_normal_multi_row(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 2, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 3, 4);
    /* Row 2: col >= 5 */
    ASSERT(wixen_selection_contains(&sel, 5, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 2, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 2, 80));
    /* Row 3: entire row */
    ASSERT(wixen_selection_contains(&sel, 0, 3, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 3, 80));
    /* Row 4: col <= 3 */
    ASSERT(wixen_selection_contains(&sel, 0, 4, 80));
    ASSERT(wixen_selection_contains(&sel, 3, 4, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 4, 80));
    PASS();
}

TEST selection_contains_line_mode(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 2, WIXEN_SEL_LINE);
    wixen_selection_update(&sel, 3, 4);
    /* All columns in rows 2-4 selected */
    ASSERT(wixen_selection_contains(&sel, 0, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 0, 3, 80));
    ASSERT(wixen_selection_contains(&sel, 0, 4, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 1, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 5, 80));
    PASS();
}

TEST selection_contains_block_mode(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 2, 1, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 6, 3);
    /* Rectangular: cols 2-6, rows 1-3 */
    ASSERT(wixen_selection_contains(&sel, 2, 1, 80));
    ASSERT(wixen_selection_contains(&sel, 4, 2, 80));
    ASSERT(wixen_selection_contains(&sel, 6, 3, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 1, 2, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 7, 2, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 0, 80));
    PASS();
}

TEST selection_contains_inactive_returns_false(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 0, 80));
    PASS();
}

TEST selection_row_count(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    ASSERT_EQ(0, (int)wixen_selection_row_count(&sel));
    wixen_selection_start(&sel, 0, 3, WIXEN_SEL_NORMAL);
    ASSERT_EQ(1, (int)wixen_selection_row_count(&sel));
    wixen_selection_update(&sel, 0, 7);
    ASSERT_EQ(5, (int)wixen_selection_row_count(&sel));
    PASS();
}

TEST selection_row_count_backward(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 7, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 0, 3);
    ASSERT_EQ(5, (int)wixen_selection_row_count(&sel));
    PASS();
}

TEST selection_block_reversed_anchor(void) {
    /* Block selection where anchor is bottom-right, end is top-left */
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 8, 5, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 2, 1);
    ASSERT(wixen_selection_contains(&sel, 4, 3, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 1, 3, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 9, 3, 80));
    PASS();
}

TEST selection_single_cell(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 5, WIXEN_SEL_NORMAL);
    /* No update — anchor == end */
    ASSERT(wixen_selection_contains(&sel, 5, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 6, 5, 80));
    PASS();
}

SUITE(selection_lifecycle) {
    RUN_TEST(selection_init_inactive);
    RUN_TEST(selection_start_activates);
    RUN_TEST(selection_update_moves_end);
    RUN_TEST(selection_clear_deactivates);
}

SUITE(selection_ordering) {
    RUN_TEST(selection_ordered_forward);
    RUN_TEST(selection_ordered_backward);
}

SUITE(selection_contains) {
    RUN_TEST(selection_contains_normal_single_row);
    RUN_TEST(selection_contains_normal_multi_row);
    RUN_TEST(selection_contains_line_mode);
    RUN_TEST(selection_contains_block_mode);
    RUN_TEST(selection_contains_inactive_returns_false);
    RUN_TEST(selection_single_cell);
    RUN_TEST(selection_block_reversed_anchor);
}

SUITE(selection_metrics) {
    RUN_TEST(selection_row_count);
    RUN_TEST(selection_row_count_backward);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(selection_lifecycle);
    RUN_SUITE(selection_ordering);
    RUN_SUITE(selection_contains);
    RUN_SUITE(selection_metrics);
    GREATEST_MAIN_END();
}
