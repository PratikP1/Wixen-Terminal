/* test_scrollback.c — Scrollback buffer with zstd compression */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/buffer.h"

static WixenRow make_row(size_t cols, const char *ch) {
    WixenRow row;
    wixen_row_init(&row, cols);
    if (ch) wixen_cell_set_content(&row.cells[0], ch);
    return row;
}

TEST sb_init_empty(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    ASSERT_EQ(0, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_push_one(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    WixenRow row = make_row(5, "A");
    wixen_scrollback_push(&sb, &row, 5);
    ASSERT_EQ(1, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_push_many(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    for (int i = 0; i < 50; i++) {
        WixenRow row = make_row(5, "X");
        wixen_scrollback_push(&sb, &row, 5);
    }
    ASSERT_EQ(50, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_compress_on_threshold(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 10);
    for (int i = 0; i < 15; i++) {
        WixenRow row = make_row(5, "Z");
        wixen_scrollback_push(&sb, &row, 5);
    }
    ASSERT_EQ(15, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) > 0);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_clear(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    for (int i = 0; i < 10; i++) {
        WixenRow row = make_row(5, "X");
        wixen_scrollback_push(&sb, &row, 5);
    }
    wixen_scrollback_clear(&sb);
    ASSERT_EQ(0, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_hot_row_access(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    WixenRow r0 = make_row(5, "A");
    WixenRow r1 = make_row(5, "B");
    wixen_scrollback_push(&sb, &r0, 5);
    wixen_scrollback_push(&sb, &r1, 5);
    const WixenRow *got = wixen_scrollback_get_hot(&sb, 0);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("A", got->cells[0].content);
    got = wixen_scrollback_get_hot(&sb, 1);
    ASSERT(got != NULL);
    ASSERT_STR_EQ("B", got->cells[0].content);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_large_push_no_crash(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 8);
    for (int i = 0; i < 200; i++) {
        WixenRow row = make_row(80, ".");
        wixen_scrollback_push(&sb, &row, 80);
    }
    ASSERT_EQ(200, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

TEST sb_zero_threshold(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 1);
    for (int i = 0; i < 5; i++) {
        WixenRow row = make_row(3, "X");
        wixen_scrollback_push(&sb, &row, 3);
    }
    ASSERT_EQ(5, (int)wixen_scrollback_len(&sb));
    wixen_scrollback_free(&sb);
    PASS();
}

SUITE(scrollback_tests) {
    RUN_TEST(sb_init_empty);
    RUN_TEST(sb_push_one);
    RUN_TEST(sb_push_many);
    RUN_TEST(sb_compress_on_threshold);
    RUN_TEST(sb_clear);
    RUN_TEST(sb_hot_row_access);
    RUN_TEST(sb_large_push_no_crash);
    RUN_TEST(sb_zero_threshold);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(scrollback_tests);
    GREATEST_MAIN_END();
}
