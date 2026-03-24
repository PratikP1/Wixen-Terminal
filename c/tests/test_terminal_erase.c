/* test_terminal_erase.c — Erase operations (ED, EL, ECH, DCH, DL, IL) */
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

static const char *cell(WixenTerminal *t, size_t row, size_t col) {
    return t->grid.rows[row].cells[col].content;
}

TEST erase_line_to_right(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;4H"); /* Col 4 */
    feed(&t, &p, "\x1b[0K");   /* EL: erase to right */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT_STR_EQ("C", cell(&t, 0, 2));
    /* Col 3 onward should be empty */
    ASSERT(cell(&t, 0, 3)[0] == '\0' || cell(&t, 0, 3)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST erase_line_to_left(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;4H"); /* Col 4 */
    feed(&t, &p, "\x1b[1K");   /* EL: erase to left */
    /* Cols 0-3 should be empty */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    ASSERT(cell(&t, 0, 3)[0] == '\0' || cell(&t, 0, 3)[0] == ' ');
    /* Col 4 onward should be intact */
    ASSERT_STR_EQ("E", cell(&t, 0, 4));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST erase_entire_line(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;4H"); /* Col 4 */
    feed(&t, &p, "\x1b[2K");   /* EL: erase entire line */
    for (int c = 0; c < 10; c++) {
        ASSERT(cell(&t, 0, c)[0] == '\0' || cell(&t, 0, c)[0] == ' ');
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST erase_display_below(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC");
    feed(&t, &p, "\x1b[2;1H"); /* Row 2 */
    feed(&t, &p, "\x1b[0J");   /* ED: erase below */
    ASSERT_STR_EQ("A", cell(&t, 0, 0)); /* Row 0 intact */
    ASSERT(cell(&t, 1, 0)[0] == '\0' || cell(&t, 1, 0)[0] == ' ');
    ASSERT(cell(&t, 2, 0)[0] == '\0' || cell(&t, 2, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST erase_display_above(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC");
    feed(&t, &p, "\x1b[2;1H"); /* Row 2 */
    feed(&t, &p, "\x1b[1J");   /* ED: erase above */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    ASSERT(cell(&t, 1, 0)[0] == '\0' || cell(&t, 1, 0)[0] == ' ');
    ASSERT_STR_EQ("C", cell(&t, 2, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST erase_display_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC");
    feed(&t, &p, "\x1b[2J"); /* ED: erase all */
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 5; c++)
            ASSERT(cell(&t, r, c)[0] == '\0' || cell(&t, r, c)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST erase_chars(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;3H"); /* Col 3 */
    feed(&t, &p, "\x1b[3X");   /* ECH: erase 3 chars */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT(cell(&t, 0, 2)[0] == '\0' || cell(&t, 0, 2)[0] == ' ');
    ASSERT(cell(&t, 0, 4)[0] == '\0' || cell(&t, 0, 4)[0] == ' ');
    ASSERT_STR_EQ("F", cell(&t, 0, 5));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST delete_chars(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;3H"); /* Col 3 */
    feed(&t, &p, "\x1b[2P");   /* DCH: delete 2 chars (shift left) */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("B", cell(&t, 0, 1));
    ASSERT_STR_EQ("E", cell(&t, 0, 2)); /* D,E shifted left */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST insert_lines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 4);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD");
    feed(&t, &p, "\x1b[2;1H"); /* Row 2 */
    feed(&t, &p, "\x1b[1L");   /* IL: insert 1 line */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    /* Row 1 should be blank (inserted) */
    ASSERT(cell(&t, 1, 0)[0] == '\0' || cell(&t, 1, 0)[0] == ' ');
    ASSERT_STR_EQ("B", cell(&t, 2, 0));
    ASSERT_STR_EQ("C", cell(&t, 3, 0));
    /* D pushed off bottom */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST delete_lines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 4);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD");
    feed(&t, &p, "\x1b[2;1H"); /* Row 2 */
    feed(&t, &p, "\x1b[1M");   /* DL: delete 1 line */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 1, 0)); /* B deleted, C moved up */
    ASSERT_STR_EQ("D", cell(&t, 2, 0));
    /* Row 3 should be blank (scrolled in from bottom) */
    ASSERT(cell(&t, 3, 0)[0] == '\0' || cell(&t, 3, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_up(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC");
    feed(&t, &p, "\x1b[1S"); /* SU: scroll up 1 */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 1, 0));
    ASSERT(cell(&t, 2, 0)[0] == '\0' || cell(&t, 2, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST scroll_down(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAAAA\r\nBBBBB\r\nCCCCC");
    feed(&t, &p, "\x1b[1T"); /* SD: scroll down 1 */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    ASSERT_STR_EQ("A", cell(&t, 1, 0));
    ASSERT_STR_EQ("B", cell(&t, 2, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_erase) {
    RUN_TEST(erase_line_to_right);
    RUN_TEST(erase_line_to_left);
    RUN_TEST(erase_entire_line);
    RUN_TEST(erase_display_below);
    RUN_TEST(erase_display_above);
    RUN_TEST(erase_display_all);
    RUN_TEST(erase_chars);
    RUN_TEST(delete_chars);
    RUN_TEST(insert_lines);
    RUN_TEST(delete_lines);
    RUN_TEST(scroll_up);
    RUN_TEST(scroll_down);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_erase);
    GREATEST_MAIN_END();
}
