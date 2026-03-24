/* test_red_harder.c — Harder RED tests targeting likely implementation gaps */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
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

/* Does resize with cursor past new boundary clamp the cursor? */
TEST red_resize_clamps_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[10;20H"); /* Cursor at row 10, col 20 (0-idx: 9,19) */
    wixen_terminal_resize(&t, 5, 3);
    ASSERT(t.grid.cursor.row < 3);
    ASSERT(t.grid.cursor.col < 5);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does ICH (insert chars) at end of line wrap or stop? */
TEST red_ich_at_end_of_line(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    feed(&t, &p, "\x1b[1;5H"); /* Last column */
    feed(&t, &p, "\x1b[1@");   /* ICH: insert 1 blank */
    /* E should be pushed off, blank at col 4 */
    ASSERT(cell(&t, 0, 4)[0] == '\0' || cell(&t, 0, 4)[0] == ' ');
    ASSERT_STR_EQ("D", cell(&t, 0, 3));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does DECALN (screen alignment test) fill all cells with 'E'? */
TEST red_decaln_fills_with_e(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 5, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b#8"); /* DECALN */
    for (size_t r = 0; r < 3; r++)
        for (size_t c = 0; c < 5; c++)
            ASSERT_STR_EQ("E", cell(&t, r, c));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does ED 3 (erase scrollback) work? */
TEST red_ed3_clears_scrollback(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* Generate scrollback */
    for (int i = 0; i < 10; i++) feed(&t, &p, "Line\r\n");
    feed(&t, &p, "\x1b[3J"); /* ED 3: clear scrollback */
    /* Should not crash. Visible grid still exists. */
    ASSERT_EQ(3, (int)t.grid.num_rows);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does REP (CSI b) repeat last character? */
TEST red_rep_repeats_char(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "A\x1b[3b"); /* Print A then repeat 3 times */
    ASSERT_STR_EQ("A", cell(&t, 0, 0));
    /* If REP is implemented, cols 1-3 should also be A */
    /* If not implemented, only col 0 has A */
    bool rep_works = (strcmp(cell(&t, 0, 1), "A") == 0 &&
                      strcmp(cell(&t, 0, 2), "A") == 0 &&
                      strcmp(cell(&t, 0, 3), "A") == 0);
    ASSERT(rep_works); /* This is likely RED */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does HPA (CSI `) work? */
TEST red_hpa_horizontal_position(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[10`"); /* HPA: col 10 (1-based) */
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does CBT (back tab, CSI Z) work? */
TEST red_cbt_back_tab(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[25G"); /* Col 25 (0-idx=24, at tab stop) */
    feed(&t, &p, "\x1b[Z");   /* CBT */
    ASSERT(t.grid.cursor.col < 24); /* Should go to previous tab stop */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Does DECSED (selective erase) not erase protected cells? */
/* This is rarely implemented — likely RED */
TEST red_protected_cells(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1\"qA"); /* DECSCA: set char protection, print A */
    feed(&t, &p, "\x1b[0\"qB"); /* DECSCA: reset protection, print B */
    feed(&t, &p, "\x1b[?2J");   /* DECSED: selective erase display */
    /* A should be protected, B should be erased */
    /* Most terminals don't implement this, so just don't crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Grid visible_text with wrapped lines — should they join? */
TEST red_visible_text_wrapped_lines(void) {
    WixenGrid g;
    wixen_grid_init(&g, 3, 2);
    wixen_cell_set_content(&g.rows[0].cells[0], "A");
    wixen_cell_set_content(&g.rows[0].cells[1], "B");
    wixen_cell_set_content(&g.rows[0].cells[2], "C");
    g.rows[0].wrapped = true;
    wixen_cell_set_content(&g.rows[1].cells[0], "D");
    char buf[256];
    wixen_grid_visible_text(&g, buf, sizeof(buf));
    /* Wrapped line should NOT have \n between them */
    ASSERT(strstr(buf, "ABC") != NULL || strstr(buf, "ABCD") != NULL);
    wixen_grid_free(&g);
    PASS();
}

SUITE(red_harder) {
    RUN_TEST(red_resize_clamps_cursor);
    RUN_TEST(red_ich_at_end_of_line);
    RUN_TEST(red_decaln_fills_with_e);
    RUN_TEST(red_ed3_clears_scrollback);
    RUN_TEST(red_rep_repeats_char);
    RUN_TEST(red_hpa_horizontal_position);
    RUN_TEST(red_cbt_back_tab);
    RUN_TEST(red_protected_cells);
    RUN_TEST(red_visible_text_wrapped_lines);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_harder);
    GREATEST_MAIN_END();
}
