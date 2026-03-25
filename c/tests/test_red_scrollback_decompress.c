/* test_red_scrollback_decompress.c — RED tests for cold tier decompression
 *
 * When rows are pushed past the hot threshold, they get zstd-compressed
 * into cold blocks. Decompressing must return the exact original content.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/core/buffer.h"

static WixenRow make_row(size_t cols, const char *text) {
    WixenRow row;
    wixen_row_init(&row, cols);
    size_t len = strlen(text);
    for (size_t i = 0; i < len && i < cols; i++) {
        char ch[2] = { text[i], '\0' };
        wixen_cell_set_content(&row.cells[i], ch);
    }
    return row;
}

TEST red_decompress_content_matches(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 5); /* Compress after 5 hot rows */

    /* Push 10 rows — first 5 should compress */
    char expected[10][32];
    for (int i = 0; i < 10; i++) {
        snprintf(expected[i], sizeof(expected[i]), "Row%d-content", i);
        WixenRow row = make_row(20, expected[i]);
        wixen_scrollback_push(&sb, &row, 20);
    }

    ASSERT_EQ(10, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) > 0);

    /* Retrieve all rows and verify content */
    for (int i = 0; i < 10; i++) {
        const WixenRow *row = wixen_scrollback_get(&sb, (size_t)i);
        ASSERT(row != NULL);
        /* Build text from cells */
        char buf[64] = {0};
        size_t pos = 0;
        for (size_t c = 0; c < 20 && row->cells[c].content[0]; c++) {
            if (strcmp(row->cells[c].content, " ") == 0) break;
            size_t clen = strlen(row->cells[c].content);
            memcpy(buf + pos, row->cells[c].content, clen);
            pos += clen;
        }
        buf[pos] = '\0';
        ASSERT_STR_EQ(expected[i], buf);
    }

    wixen_scrollback_free(&sb);
    PASS();
}

TEST red_decompress_after_many_compressions(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 4);

    /* Push 100 rows — many compression cycles */
    for (int i = 0; i < 100; i++) {
        char text[32];
        snprintf(text, sizeof(text), "L%03d", i);
        WixenRow row = make_row(10, text);
        wixen_scrollback_push(&sb, &row, 10);
    }

    ASSERT_EQ(100, (int)wixen_scrollback_len(&sb));

    /* Spot-check first, middle, last */
    const WixenRow *first = wixen_scrollback_get(&sb, 0);
    ASSERT(first != NULL);
    ASSERT(first->cells[0].content[0] == 'L');

    const WixenRow *mid = wixen_scrollback_get(&sb, 50);
    ASSERT(mid != NULL);

    const WixenRow *last = wixen_scrollback_get(&sb, 99);
    ASSERT(last != NULL);

    wixen_scrollback_free(&sb);
    PASS();
}

TEST red_decompress_preserves_wide_chars(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 3);

    /* Row with a wide char */
    WixenRow row;
    wixen_row_init(&row, 10);
    wixen_cell_set_content(&row.cells[0], "A");
    wixen_cell_set_content(&row.cells[1], "\xe4\xb8\x96"); /* 世 */
    row.cells[1].width = 2;
    wixen_cell_set_content(&row.cells[2], "");
    row.cells[2].width = 0;
    wixen_cell_set_content(&row.cells[3], "B");

    wixen_scrollback_push(&sb, &row, 10);

    /* Push more to force compression */
    for (int i = 0; i < 5; i++) {
        WixenRow filler = make_row(10, "filler");
        wixen_scrollback_push(&sb, &filler, 10);
    }

    /* Retrieve the wide char row */
    const WixenRow *retrieved = wixen_scrollback_get(&sb, 0);
    ASSERT(retrieved != NULL);
    ASSERT_STR_EQ("A", retrieved->cells[0].content);
    ASSERT_STR_EQ("\xe4\xb8\x96", retrieved->cells[1].content);
    ASSERT_EQ(2, (int)retrieved->cells[1].width);
    ASSERT_STR_EQ("B", retrieved->cells[3].content);

    wixen_scrollback_free(&sb);
    PASS();
}

SUITE(red_scrollback_decompress) {
    RUN_TEST(red_decompress_content_matches);
    RUN_TEST(red_decompress_after_many_compressions);
    RUN_TEST(red_decompress_preserves_wide_chars);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_scrollback_decompress);
    GREATEST_MAIN_END();
}
