/* test_cursor.c — Tests for WixenCursor */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/cursor.h"

TEST cursor_init_defaults(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    ASSERT_EQ(0, (int)cur.col);
    ASSERT_EQ(0, (int)cur.row);
    ASSERT(cur.visible);
    ASSERT_EQ(WIXEN_CURSOR_BLOCK, cur.shape);
    ASSERT(cur.blinking);
    PASS();
}

TEST cursor_move_to(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    wixen_cursor_move_to(&cur, 10, 5);
    ASSERT_EQ(10, (int)cur.col);
    ASSERT_EQ(5, (int)cur.row);
    PASS();
}

TEST cursor_clamp_within_bounds(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    wixen_cursor_move_to(&cur, 5, 3);
    wixen_cursor_clamp(&cur, 80, 24);
    ASSERT_EQ(5, (int)cur.col);
    ASSERT_EQ(3, (int)cur.row);
    PASS();
}

TEST cursor_clamp_overflow_col(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    wixen_cursor_move_to(&cur, 100, 3);
    wixen_cursor_clamp(&cur, 80, 24);
    ASSERT_EQ(79, (int)cur.col);
    ASSERT_EQ(3, (int)cur.row);
    PASS();
}

TEST cursor_clamp_overflow_row(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    wixen_cursor_move_to(&cur, 5, 30);
    wixen_cursor_clamp(&cur, 80, 24);
    ASSERT_EQ(5, (int)cur.col);
    ASSERT_EQ(23, (int)cur.row);
    PASS();
}

TEST cursor_clamp_overflow_both(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    wixen_cursor_move_to(&cur, 200, 200);
    wixen_cursor_clamp(&cur, 80, 24);
    ASSERT_EQ(79, (int)cur.col);
    ASSERT_EQ(23, (int)cur.row);
    PASS();
}

TEST cursor_clamp_zero_size_grid(void) {
    /* Edge case: clamp with 0-size grid should not crash */
    WixenCursor cur;
    wixen_cursor_init(&cur);
    wixen_cursor_move_to(&cur, 5, 5);
    wixen_cursor_clamp(&cur, 0, 0);
    /* With 0 grid, position stays (no valid position exists) */
    ASSERT_EQ(5, (int)cur.col);
    ASSERT_EQ(5, (int)cur.row);
    PASS();
}

TEST cursor_shape_values(void) {
    WixenCursor cur;
    wixen_cursor_init(&cur);
    cur.shape = WIXEN_CURSOR_UNDERLINE;
    ASSERT_EQ(WIXEN_CURSOR_UNDERLINE, cur.shape);
    cur.shape = WIXEN_CURSOR_BAR;
    ASSERT_EQ(WIXEN_CURSOR_BAR, cur.shape);
    PASS();
}

SUITE(cursor_tests) {
    RUN_TEST(cursor_init_defaults);
    RUN_TEST(cursor_move_to);
    RUN_TEST(cursor_clamp_within_bounds);
    RUN_TEST(cursor_clamp_overflow_col);
    RUN_TEST(cursor_clamp_overflow_row);
    RUN_TEST(cursor_clamp_overflow_both);
    RUN_TEST(cursor_clamp_zero_size_grid);
    RUN_TEST(cursor_shape_values);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(cursor_tests);
    GREATEST_MAIN_END();
}
