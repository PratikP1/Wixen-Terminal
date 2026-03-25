/* test_red_overflow.c — RED tests for integer overflow and boundary conditions
 *
 * These test extreme inputs that could cause integer overflow, buffer
 * overrun, or undefined behavior in C. Rust catches these at compile
 * time or panics at runtime. C silently corrupts memory.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/buffer.h"
#include "wixen/core/selection.h"
#include "wixen/vt/parser.h"
#include "wixen/a11y/events.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* === Zero-size grid === */

TEST red_grid_zero_cols(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 0, 0);
    /* Should handle gracefully — no crash */
    char *text = wixen_terminal_visible_text(&t);
    ASSERT(text != NULL);
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_grid_one_by_one(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 1, 1);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEF"); /* Way more than 1x1 grid can hold */
    char *text = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(text != NULL);
    /* Last char should be F (each overwrites the single cell) */
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Huge CSI parameter === */

TEST red_huge_csi_param(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* CSI 999999999 A — move up 999999999 lines */
    feed(&t, &p, "\x1b[999999999A");
    /* Cursor should be clamped to row 0, not underflow */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    /* CSI 999999999 B — move down a billion */
    feed(&t, &p, "\x1b[999999999B");
    /* Should clamp to last row */
    ASSERT_EQ(23, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_huge_csi_column(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* CHA with huge param */
    feed(&t, &p, "\x1b[999999999G");
    /* Should clamp to last column */
    ASSERT(t.grid.cursor.col <= 79);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Very long OSC string === */

TEST red_long_osc_title(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Build a 10KB title */
    size_t title_len = 10000;
    char *input = malloc(5 + title_len + 2);
    ASSERT(input != NULL);
    memcpy(input, "\x1b]0;", 4);
    memset(input + 4, 'A', title_len);
    input[4 + title_len] = '\x07';
    input[5 + title_len] = '\0';
    feed(&t, &p, input);
    /* Title should be set (possibly truncated) without crash */
    ASSERT(t.title != NULL);
    ASSERT(strlen(t.title) > 0);
    free(input);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === OSC 52 large base64 payload === */

TEST red_osc52_large_payload(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* 8KB of base64 'A' characters (= 6KB decoded) */
    size_t b64_len = 8000;
    size_t total = 7 + b64_len + 2;
    char *input = malloc(total);
    ASSERT(input != NULL);
    memcpy(input, "\x1b]52;c;", 7);
    memset(input + 7, 'Q', b64_len); /* 'Q' = valid base64 */
    input[7 + b64_len] = '\x07';
    input[8 + b64_len] = '\0';
    feed(&t, &p, input);
    char *clip = wixen_terminal_drain_clipboard_write(&t);
    ASSERT(clip != NULL);
    ASSERT(strlen(clip) > 1000); /* Should have decoded content */
    free(clip);
    free(input);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Parser with split UTF-8 === */

TEST red_parser_split_utf8(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* U+4E16 (世) = E4 B8 96 in UTF-8. Feed in two chunks: */
    const uint8_t chunk1[] = { 0xE4 };
    const uint8_t chunk2[] = { 0xB8, 0x96 };
    WixenAction actions[16];
    size_t c1 = wixen_parser_process(&p, chunk1, 1, actions, 16);
    /* First chunk shouldn't produce a print action (incomplete UTF-8) */
    for (size_t i = 0; i < c1; i++) {
        wixen_terminal_dispatch(&t, &actions[i]);
    }
    size_t c2 = wixen_parser_process(&p, chunk2, 2, actions, 16);
    for (size_t i = 0; i < c2; i++) {
        wixen_terminal_dispatch(&t, &actions[i]);
    }
    /* Cell should contain the CJK character */
    char *row = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(row != NULL);
    ASSERT(strlen(row) > 0);
    free(row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Resize to huge then back === */

TEST red_resize_huge_then_small(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    /* Resize to 1000x500 — lots of memory */
    wixen_terminal_resize(&t, 1000, 500);
    ASSERT_EQ(1000, (int)t.grid.cols);
    ASSERT_EQ(500, (int)t.grid.num_rows);
    /* Resize back to small */
    wixen_terminal_resize(&t, 10, 5);
    ASSERT_EQ(10, (int)t.grid.cols);
    ASSERT_EQ(5, (int)t.grid.num_rows);
    wixen_terminal_free(&t);
    PASS();
}

/* === Selection with reversed endpoints === */

TEST red_selection_reversed(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    /* Start at (10,5), drag to (2,1) — reversed */
    wixen_selection_start(&sel, 10, 5, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 2, 1);
    WixenGridPoint start, end;
    wixen_selection_ordered(&sel, &start, &end);
    /* Ordered should normalize */
    ASSERT(start.row <= end.row);
    if (start.row == end.row) ASSERT(start.col <= end.col);
    PASS();
}

/* === Scrollback buffer push MAX items === */

TEST red_scrollback_many_items(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 10);
    for (int i = 0; i < 1000; i++) {
        WixenRow row;
        wixen_row_init(&row, 5);
        wixen_scrollback_push(&sb, &row, 5);
    }
    ASSERT_EQ(1000, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) > 0);
    wixen_scrollback_free(&sb);
    PASS();
}

/* === Strip VT on huge input === */

TEST red_strip_vt_huge(void) {
    /* 100KB of escape sequences */
    size_t len = 100000;
    char *input = malloc(len + 1);
    ASSERT(input != NULL);
    for (size_t i = 0; i < len; i += 4) {
        input[i] = '\x1b'; input[i+1] = '['; input[i+2] = '1'; input[i+3] = 'm';
    }
    input[len] = '\0';
    char *stripped = wixen_strip_vt_escapes(input);
    ASSERT(stripped != NULL);
    /* All escape sequences — should be empty after stripping */
    ASSERT_EQ(0, (int)strlen(stripped));
    free(stripped);
    free(input);
    PASS();
}

SUITE(red_overflow) {
    RUN_TEST(red_grid_zero_cols);
    RUN_TEST(red_grid_one_by_one);
    RUN_TEST(red_huge_csi_param);
    RUN_TEST(red_huge_csi_column);
    RUN_TEST(red_long_osc_title);
    RUN_TEST(red_osc52_large_payload);
    RUN_TEST(red_parser_split_utf8);
    RUN_TEST(red_resize_huge_then_small);
    RUN_TEST(red_selection_reversed);
    RUN_TEST(red_scrollback_many_items);
    RUN_TEST(red_strip_vt_huge);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_overflow);
    GREATEST_MAIN_END();
}
