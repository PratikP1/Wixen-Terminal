/* test_buffer_extended.c — Additional scrollback buffer tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/buffer.h"

static WixenRow make_row(size_t cols, const char *first_cell) {
    WixenRow row;
    wixen_row_init(&row, cols);
    if (first_cell) wixen_cell_set_content(&row.cells[0], first_cell);
    return row;
}

TEST buffer_hot_order_preserved(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    for (int i = 0; i < 5; i++) {
        char ch[8]; sprintf(ch, "%d", i);
        WixenRow row = make_row(5, ch);
        wixen_scrollback_push(&sb, &row, 5);
    }
    /* Hot rows should be in push order */
    ASSERT_STR_EQ("0", wixen_scrollback_get_hot(&sb, 0)->cells[0].content);
    ASSERT_STR_EQ("4", wixen_scrollback_get_hot(&sb, 4)->cells[0].content);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_push_after_clear(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    WixenRow row = make_row(5, "A");
    wixen_scrollback_push(&sb, &row, 5);
    wixen_scrollback_clear(&sb);
    row = make_row(5, "B");
    wixen_scrollback_push(&sb, &row, 5);
    ASSERT_EQ(1, (int)wixen_scrollback_len(&sb));
    ASSERT_STR_EQ("B", wixen_scrollback_get_hot(&sb, 0)->cells[0].content);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_threshold_boundary(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 5);
    /* Push exactly threshold */
    for (int i = 0; i < 5; i++) {
        WixenRow row = make_row(3, "X");
        wixen_scrollback_push(&sb, &row, 3);
    }
    ASSERT_EQ(5, (int)wixen_scrollback_len(&sb));
    /* No compression yet (at threshold, not over) */
    ASSERT_EQ(0, (int)wixen_scrollback_cold_blocks(&sb));
    /* One more triggers compression */
    WixenRow row = make_row(3, "Y");
    wixen_scrollback_push(&sb, &row, 3);
    ASSERT_EQ(6, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) > 0);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_multiple_compressions(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 4);
    for (int i = 0; i < 20; i++) {
        WixenRow row = make_row(3, "Z");
        wixen_scrollback_push(&sb, &row, 3);
    }
    ASSERT_EQ(20, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) >= 2);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST buffer_free_idempotent(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    wixen_scrollback_free(&sb);
    /* Second free should be safe */
    wixen_scrollback_free(&sb);
    PASS();
}

SUITE(buffer_extended) {
    RUN_TEST(buffer_hot_order_preserved);
    RUN_TEST(buffer_push_after_clear);
    RUN_TEST(buffer_threshold_boundary);
    RUN_TEST(buffer_multiple_compressions);
    RUN_TEST(buffer_free_idempotent);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(buffer_extended);
    GREATEST_MAIN_END();
}
