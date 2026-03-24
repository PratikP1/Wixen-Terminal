/* test_terminal_utf8.c — UTF-8 character handling in terminal */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[256];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 256);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST utf8_ascii(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello");
    ASSERT_STR_EQ("H", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("e", t.grid.rows[0].cells[1].content);
    ASSERT_EQ(5, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_two_byte(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\xc3\xa9"); /* é */
    ASSERT_STR_EQ("\xc3\xa9", t.grid.rows[0].cells[0].content);
    ASSERT_EQ(1, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_three_byte(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\xe2\x9c\x93"); /* ✓ */
    ASSERT_STR_EQ("\xe2\x9c\x93", t.grid.rows[0].cells[0].content);
    ASSERT_EQ(1, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_four_byte_emoji(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\xf0\x9f\x98\x80"); /* 😀 */
    /* Emoji should be stored in cell */
    ASSERT(strlen(t.grid.rows[0].cells[0].content) > 0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_cjk_double_width(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\xe4\xb8\xad"); /* 中 (CJK, width 2) */
    /* Should occupy 2 columns */
    ASSERT_EQ(2, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_mixed_ascii_and_multi(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "caf\xc3\xa9"); /* "café" */
    ASSERT_STR_EQ("c", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("a", t.grid.rows[0].cells[1].content);
    ASSERT_STR_EQ("f", t.grid.rows[0].cells[2].content);
    ASSERT_STR_EQ("\xc3\xa9", t.grid.rows[0].cells[3].content);
    ASSERT_EQ(4, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_overwrite_existing(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    feed(&t, &p, "\x1b[1;3H"); /* Back to col 3 */
    feed(&t, &p, "\xc3\xbc"); /* ü overwrites C */
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", t.grid.rows[0].cells[1].content);
    ASSERT_STR_EQ("\xc3\xbc", t.grid.rows[0].cells[2].content);
    ASSERT_STR_EQ("D", t.grid.rows[0].cells[3].content);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST utf8_at_line_boundary(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 3, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AB\xc3\xa9"); /* AB + é — é at col 2, then wraps */
    /* é should be at last column of row 0 */
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", t.grid.rows[0].cells[1].content);
    ASSERT_STR_EQ("\xc3\xa9", t.grid.rows[0].cells[2].content);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_utf8) {
    RUN_TEST(utf8_ascii);
    RUN_TEST(utf8_two_byte);
    RUN_TEST(utf8_three_byte);
    RUN_TEST(utf8_four_byte_emoji);
    RUN_TEST(utf8_cjk_double_width);
    RUN_TEST(utf8_mixed_ascii_and_multi);
    RUN_TEST(utf8_overwrite_existing);
    RUN_TEST(utf8_at_line_boundary);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_utf8);
    GREATEST_MAIN_END();
}
