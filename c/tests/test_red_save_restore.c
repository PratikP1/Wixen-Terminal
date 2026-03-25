/* test_red_save_restore.c — RED tests for DECSC/DECRC completeness
 *
 * DECSC (ESC 7) saves: cursor position, SGR attributes, charset, origin mode, autowrap.
 * DECRC (ESC 8) restores all of the above.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/cell.h"
#include "wixen/vt/parser.h"

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_save_restore_position(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;10H"); /* Row 4, col 9 */
    feeds(&t, &p, "\x1b" "7");   /* DECSC */
    feeds(&t, &p, "\x1b[1;1H");  /* Home */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    feeds(&t, &p, "\x1b" "8");   /* DECRC */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_save_restore_sgr(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[1;31m"); /* Bold + red fg */
    feeds(&t, &p, "\x1b" "7");   /* Save */
    feeds(&t, &p, "\x1b[0m");    /* Reset attrs */
    ASSERT_FALSE(t.grid.current_attrs.bold);
    feeds(&t, &p, "\x1b" "8");   /* Restore */
    /* Bold and red should be restored */
    ASSERT(t.grid.current_attrs.bold);
    ASSERT(t.grid.current_attrs.fg.type != WIXEN_COLOR_DEFAULT);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_save_restore_charset(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b(0");     /* Switch to DEC Special */
    ASSERT_EQ(1, (int)t.charsets[0]);
    feeds(&t, &p, "\x1b" "7");   /* Save */
    feeds(&t, &p, "\x1b(B");     /* Back to ASCII */
    ASSERT_EQ(0, (int)t.charsets[0]);
    feeds(&t, &p, "\x1b" "8");   /* Restore */
    ASSERT_EQ(1, (int)t.charsets[0]);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_save_restore_origin_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?6h");   /* Enable origin mode */
    feeds(&t, &p, "\x1b" "7");   /* Save */
    feeds(&t, &p, "\x1b[?6l");   /* Disable origin mode */
    ASSERT_FALSE(t.modes.origin_mode);
    feeds(&t, &p, "\x1b" "8");   /* Restore */
    ASSERT(t.modes.origin_mode);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_save_restore_autowrap(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?7l");   /* Disable autowrap */
    feeds(&t, &p, "\x1b" "7");
    feeds(&t, &p, "\x1b[?7h");   /* Re-enable */
    ASSERT(t.modes.auto_wrap);
    feeds(&t, &p, "\x1b" "8");
    ASSERT_FALSE(t.modes.auto_wrap);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_restore_without_save(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;5H");
    /* DECRC without prior DECSC — should not crash */
    feeds(&t, &p, "\x1b" "8");
    /* Behavior is implementation-defined but must not crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_save_restore) {
    RUN_TEST(red_save_restore_position);
    RUN_TEST(red_save_restore_sgr);
    RUN_TEST(red_save_restore_charset);
    RUN_TEST(red_save_restore_origin_mode);
    RUN_TEST(red_save_restore_autowrap);
    RUN_TEST(red_restore_without_save);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_save_restore);
    GREATEST_MAIN_END();
}
