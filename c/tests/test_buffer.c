/* test_buffer.c — Tests for scrollback buffer */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/buffer.h"

/* Helper: create a row with content in first cell */
static WixenRow make_row(size_t cols, const char *first_cell) {
    WixenRow row;
    wixen_row_init(&row, cols);
    if (first_cell) wixen_cell_set_content(&row.cells[0], first_cell);
    return row;
}

TEST buffer_init_empty(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    ASSERT_EQ(0, (int)wixen_scrollback_len(&sb));
    ASSERT_EQ(0, (int)wixen_scrollback_hot_len(&sb));
    ASSERT_EQ(0, (int)wixen_scrollback_cold_blocks(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_push_increments_len(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    WixenRow row = make_row(10, "A");
    wixen_scrollback_push(&sb, &row, 10);
    ASSERT_EQ(1, (int)wixen_scrollback_len(&sb));
    ASSERT_EQ(1, (int)wixen_scrollback_hot_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_push_multiple(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    for (int i = 0; i < 10; i++) {
        char ch[2] = { 'A' + (char)i, 0 };
        WixenRow row = make_row(5, ch);
        wixen_scrollback_push(&sb, &row, 5);
    }
    ASSERT_EQ(10, (int)wixen_scrollback_len(&sb));
    ASSERT_EQ(10, (int)wixen_scrollback_hot_len(&sb));
    ASSERT_EQ(0, (int)wixen_scrollback_cold_blocks(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_get_hot(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    WixenRow r1 = make_row(5, "X");
    WixenRow r2 = make_row(5, "Y");
    wixen_scrollback_push(&sb, &r1, 5);
    wixen_scrollback_push(&sb, &r2, 5);
    const WixenRow *got = wixen_scrollback_get_hot(&sb, 0);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("X", got->cells[0].content);
    got = wixen_scrollback_get_hot(&sb, 1);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("Y", got->cells[0].content);
    ASSERT(wixen_scrollback_get_hot(&sb, 2) == NULL);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_compression_triggers(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 10); /* Small threshold for testing */
    for (int i = 0; i < 12; i++) {
        char ch[2] = { 'A' + (char)(i % 26), 0 };
        WixenRow row = make_row(5, ch);
        wixen_scrollback_push(&sb, &row, 5);
    }
    /* After 11 pushes (>10 threshold), oldest half (5) should compress */
    ASSERT_EQ(12, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) > 0);
    ASSERT(wixen_scrollback_hot_len(&sb) < 12);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_clear_resets(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    for (int i = 0; i < 5; i++) {
        WixenRow row = make_row(5, "X");
        wixen_scrollback_push(&sb, &row, 5);
    }
    ASSERT_EQ(5, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_clear(&sb);
    ASSERT_EQ(0, (int)wixen_scrollback_len(&sb));
    ASSERT_EQ(0, (int)wixen_scrollback_hot_len(&sb));
    ASSERT_EQ(0, (int)wixen_scrollback_cold_blocks(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_push_transfers_ownership(void) {
    /* After push, the original row should be zeroed out */
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    WixenRow row = make_row(5, "Test");
    wixen_scrollback_push(&sb, &row, 5);
    ASSERT(row.cells == NULL); /* Ownership transferred */
    ASSERT_EQ(0, (int)row.count);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_large_compression_cycle(void) {
    /* Push many rows, ensure multiple compression cycles work */
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 10);
    for (int i = 0; i < 50; i++) {
        char ch[4];
        ch[0] = 'A' + (char)(i % 26);
        ch[1] = '0' + (char)(i / 26);
        ch[2] = '\0';
        WixenRow row = make_row(5, ch);
        wixen_scrollback_push(&sb, &row, 5);
    }
    ASSERT_EQ(50, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) >= 2);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_get_hot_out_of_range(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    ASSERT(wixen_scrollback_get_hot(&sb, 0) == NULL);
    ASSERT(wixen_scrollback_get_hot(&sb, 999) == NULL);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_default_threshold(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 0); /* 0 → default 10000 */
    ASSERT_EQ(10000, (int)sb.hot_threshold);
    wixen_scrollback_free(&sb);
    PASS();
}

SUITE(buffer_tests) {
    RUN_TEST(buffer_init_empty);
    RUN_TEST(buffer_push_increments_len);
    RUN_TEST(buffer_push_multiple);
    RUN_TEST(buffer_get_hot);
    RUN_TEST(buffer_compression_triggers);
    RUN_TEST(buffer_clear_resets);
    RUN_TEST(buffer_push_transfers_ownership);
    RUN_TEST(buffer_large_compression_cycle);
    RUN_TEST(buffer_get_hot_out_of_range);
    RUN_TEST(buffer_default_threshold);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(buffer_tests);
    GREATEST_MAIN_END();
}
