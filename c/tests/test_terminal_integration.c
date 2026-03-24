/* test_terminal_integration.c — End-to-end terminal scenarios */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

static const char *cell(WixenTerminal *t, size_t row, size_t col) {
    return t->grid.rows[row].cells[col].content;
}

TEST integ_clear_screen_and_home(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ\r\nXYZ");
    feed(&t, &p, "\x1b[2J\x1b[H"); /* Clear screen + home */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    /* Screen should be clear */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_colored_text(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31mRed\x1b[32mGreen\x1b[0mNormal");
    ASSERT_STR_EQ("R", cell(&t, 0, 0));
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[0].attrs.fg.type);
    ASSERT_EQ(1, (int)t.grid.rows[0].cells[0].attrs.fg.index); /* Red */
    ASSERT_STR_EQ("G", cell(&t, 0, 3));
    ASSERT_EQ(2, (int)t.grid.rows[0].cells[3].attrs.fg.index); /* Green */
    ASSERT_STR_EQ("N", cell(&t, 0, 8));
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[8].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_dir_listing_simulation(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "C:\\Users\\test> dir\r\n");
    feed(&t, &p, " Volume in drive C has no label.\r\n");
    feed(&t, &p, " Directory of C:\\Users\\test\r\n\r\n");
    feed(&t, &p, "03/23/2026  file1.txt\r\n");
    feed(&t, &p, "03/23/2026  file2.txt\r\n");
    ASSERT_EQ(6, (int)t.grid.cursor.row);
    ASSERT_STR_EQ("C", cell(&t, 0, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_cursor_save_restore_with_attrs(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;31m"); /* Bold + red */
    feed(&t, &p, "\x1b[5;10H"); /* Move */
    feed(&t, &p, "\x1b" "7");   /* Save */
    feed(&t, &p, "\x1b[0m");    /* Reset */
    feed(&t, &p, "\x1b[1;1H");  /* Home */
    feed(&t, &p, "\x1b" "8");   /* Restore */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_scroll_region_vim_like(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* Simulate status line at bottom */
    feed(&t, &p, "\x1b[1;4r");   /* Scroll region: rows 1-4 */
    feed(&t, &p, "\x1b[1;1H");
    feed(&t, &p, "Line1\r\nLine2\r\nLine3\r\nLine4");
    feed(&t, &p, "\x1b[5;1H");   /* Move to row 5 (status) */
    feed(&t, &p, "-- INSERT --");
    ASSERT_STR_EQ("-", cell(&t, 4, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_alt_screen_toggle(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello");
    ASSERT_STR_EQ("H", cell(&t, 0, 0));
    feed(&t, &p, "\x1b[?1049h"); /* Enter alt screen */
    ASSERT(t.modes.alternate_screen);
    /* Alt screen is clean */
    ASSERT(cell(&t, 0, 0)[0] == '\0' || cell(&t, 0, 0)[0] == ' ');
    feed(&t, &p, "Alt");
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    feed(&t, &p, "\x1b[?1049l"); /* Leave alt screen */
    ASSERT_FALSE(t.modes.alternate_screen);
    /* Original content restored */
    ASSERT_STR_EQ("H", cell(&t, 0, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_tab_stops_and_erase(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Col1\tCol2\tCol3");
    ASSERT_STR_EQ("C", cell(&t, 0, 0));
    ASSERT_STR_EQ("C", cell(&t, 0, 8));
    ASSERT_STR_EQ("C", cell(&t, 0, 16));
    /* Erase to end of line */
    feed(&t, &p, "\x1b[1;9H\x1b[K");
    ASSERT_STR_EQ("C", cell(&t, 0, 0)); /* First tab col intact */
    ASSERT(cell(&t, 0, 8)[0] == '\0' || cell(&t, 0, 8)[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_rapid_newlines(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    for (int i = 0; i < 100; i++) {
        char line[32];
        snprintf(line, sizeof(line), "Line %d\r\n", i);
        feed(&t, &p, line);
    }
    /* Should not crash. Last visible lines should be 96-99 */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_mixed_control_and_text(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "A\x08" "B"); /* A, backspace, B → B at col 0 */
    ASSERT_STR_EQ("B", cell(&t, 0, 0));
    ASSERT_EQ(1, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_long_line_wraps(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJKLMNO"); /* 15 chars in 5-col grid = 3 rows */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    ASSERT_STR_EQ("F", cell(&t, 1, 0));
    ASSERT_STR_EQ("K", cell(&t, 2, 0));
    ASSERT(t.grid.rows[0].wrapped);
    ASSERT(t.grid.rows[1].wrapped);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST integ_insert_delete_chars(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDEFGHIJ");
    feed(&t, &p, "\x1b[1;3H");  /* Col 3 */
    feed(&t, &p, "\x1b[2P");    /* Delete 2 chars */
    ASSERT_STR_EQ("E", cell(&t, 0, 2)); /* C,D deleted, E shifts to col 2 */
    feed(&t, &p, "\x1b[1;1H");  /* Home */
    feed(&t, &p, "\x1b[4h");    /* Insert mode */
    feed(&t, &p, "XY");         /* Insert XY */
    ASSERT_STR_EQ("X", cell(&t, 0, 0));
    ASSERT_STR_EQ("Y", cell(&t, 0, 1));
    ASSERT_STR_EQ("A", cell(&t, 0, 2));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_integration) {
    RUN_TEST(integ_clear_screen_and_home);
    RUN_TEST(integ_colored_text);
    RUN_TEST(integ_dir_listing_simulation);
    RUN_TEST(integ_cursor_save_restore_with_attrs);
    RUN_TEST(integ_scroll_region_vim_like);
    RUN_TEST(integ_alt_screen_toggle);
    RUN_TEST(integ_tab_stops_and_erase);
    RUN_TEST(integ_rapid_newlines);
    RUN_TEST(integ_mixed_control_and_text);
    RUN_TEST(integ_long_line_wraps);
    RUN_TEST(integ_insert_delete_chars);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_integration);
    GREATEST_MAIN_END();
}
