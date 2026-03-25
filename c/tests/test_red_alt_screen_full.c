/* test_red_alt_screen_full.c — RED tests for complete alt screen behavior
 *
 * Alt screen (DECSET 1049) must save/restore: cursor position, cursor
 * attrs, charset, modes, scroll region — not just grid content.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_alt_saves_cursor_position(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[5;10H"); /* row 4, col 9 */
    size_t main_row = t.grid.cursor.row;
    size_t main_col = t.grid.cursor.col;
    feeds(&t, &p, "\x1b[?1049h"); /* Enter alt */
    feeds(&t, &p, "\x1b[1;1H");   /* Move cursor on alt */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    feeds(&t, &p, "\x1b[?1049l"); /* Exit alt */
    ASSERT_EQ(main_row, t.grid.cursor.row);
    ASSERT_EQ(main_col, t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_alt_clears_screen(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "Main content here");
    feeds(&t, &p, "\x1b[?1049h");
    /* Alt screen should start blank */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT_STR_EQ("", r0);
    free(r0);
    feeds(&t, &p, "\x1b[?1049l");
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_alt_restores_content(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "Preserved!");
    feeds(&t, &p, "\x1b[?1049h");
    feeds(&t, &p, "Alt text");
    feeds(&t, &p, "\x1b[?1049l");
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "Preserved!") != NULL);
    free(r0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_alt_saves_scroll_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[3;8r"); /* Set scroll region rows 3-8 */
    ASSERT_EQ(2, (int)t.scroll_region.top);
    ASSERT_EQ(8, (int)t.scroll_region.bottom);
    feeds(&t, &p, "\x1b[?1049h");
    /* Alt screen should reset scroll region */
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(10, (int)t.scroll_region.bottom);
    feeds(&t, &p, "\x1b[?1049l");
    /* Main screen scroll region restored */
    ASSERT_EQ(2, (int)t.scroll_region.top);
    ASSERT_EQ(8, (int)t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_alt_saves_origin_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b[?6h"); /* Enable origin mode */
    ASSERT(t.modes.origin_mode);
    feeds(&t, &p, "\x1b[?1049h");
    /* Alt should start with origin mode off */
    ASSERT_FALSE(t.modes.origin_mode);
    feeds(&t, &p, "\x1b[?1049l");
    /* Restored */
    ASSERT(t.modes.origin_mode);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_alt_nested_noop(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feeds(&t, &p, "Original");
    feeds(&t, &p, "\x1b[?1049h");
    feeds(&t, &p, "Alt1");
    /* Second enter should be a noop — already in alt */
    feeds(&t, &p, "\x1b[?1049h");
    feeds(&t, &p, "Alt2");
    /* Single exit should return to main */
    feeds(&t, &p, "\x1b[?1049l");
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "Original") != NULL);
    free(r0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_alt_screen_full) {
    RUN_TEST(red_alt_saves_cursor_position);
    RUN_TEST(red_alt_clears_screen);
    RUN_TEST(red_alt_restores_content);
    RUN_TEST(red_alt_saves_scroll_region);
    RUN_TEST(red_alt_saves_origin_mode);
    RUN_TEST(red_alt_nested_noop);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_alt_screen_full);
    GREATEST_MAIN_END();
}
