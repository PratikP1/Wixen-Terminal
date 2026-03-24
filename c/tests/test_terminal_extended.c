/* test_terminal_extended.c — Additional terminal tests for completeness */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static WixenParser parser;
static void feed(WixenTerminal *t, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(&parser, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) free(actions[i].apc.data);
    }
}
static const char *row_text(const WixenTerminal *t, size_t row) {
    static char buf[4096];
    if (row >= t->grid.num_rows) return "";
    wixen_row_text(&t->grid.rows[row], buf, sizeof(buf));
    return buf;
}
static void setup(void) { wixen_parser_init(&parser); }
static void teardown(void) { wixen_parser_free(&parser); }

/* === Erase in display mode 1 (start to cursor) === */
TEST ed_mode_1(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 3); setup();
    feed(&t, "Line0\r\nLine1\r\nLine2");
    feed(&t, "\x1b[2;3H"); /* Row 2, col 3 */
    feed(&t, "\x1b[1J");   /* ED mode 1: clear start to cursor */
    ASSERT_STR_EQ("", row_text(&t, 0)); /* Row 0 cleared */
    /* Row 1 partially cleared up to cursor col */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Line Feed New Line mode (LNM) === */
TEST lnm_mode(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[20h"); /* Enable LNM */
    ASSERT(t.modes.line_feed_new_line);
    feed(&t, "ABC");
    ASSERT_EQ(3, (int)t.grid.cursor.col);
    feed(&t, "\n"); /* LF should also do CR in LNM mode */
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Application cursor keys (DECCKM) === */
TEST decckm_mode(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    ASSERT_FALSE(t.modes.cursor_keys_application);
    feed(&t, "\x1b[?1h"); /* Enable DECCKM */
    ASSERT(t.modes.cursor_keys_application);
    feed(&t, "\x1b[?1l");
    ASSERT_FALSE(t.modes.cursor_keys_application);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Multiple SGR in one sequence === */
TEST sgr_multiple_combined(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[1;3;4;31;42m"); /* Bold+italic+underline+red fg+green bg */
    ASSERT(t.grid.current_attrs.bold);
    ASSERT(t.grid.current_attrs.italic);
    ASSERT_EQ(WIXEN_UNDERLINE_SINGLE, t.grid.current_attrs.underline);
    ASSERT_EQ(1, t.grid.current_attrs.fg.index);
    ASSERT_EQ(2, t.grid.current_attrs.bg.index);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === CR at col 0 is no-op === */
TEST cr_at_zero(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    feed(&t, "\r");
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Backspace doesn't go negative === */
TEST bs_at_zero(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x08\x08\x08");
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === CUP with large params clamps === */
TEST cup_large_params(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[9999;9999H");
    ASSERT_EQ(23, (int)t.grid.cursor.row);
    ASSERT_EQ(79, (int)t.grid.cursor.col);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Erase line mode 2 from various positions === */
TEST el_mode2_mid_line(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1); setup();
    feed(&t, "ABCDEFGHIJ");
    feed(&t, "\x1b[5G"); /* Col 5 */
    feed(&t, "\x1b[2K"); /* Erase entire line */
    ASSERT_STR_EQ("", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Print overwrites existing content === */
TEST print_overwrites(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 1); setup();
    feed(&t, "ABCDEFGHIJ");
    feed(&t, "\x1b[1G"); /* Home */
    feed(&t, "XYZ");
    ASSERT_STR_EQ("XYZDEFGHIJ", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Multiple responses queued === */
TEST multiple_responses(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[5n"); /* Status report */
    feed(&t, "\x1b[6n"); /* CPR */
    const char *r1 = wixen_terminal_pop_response(&t);
    const char *r2 = wixen_terminal_pop_response(&t);
    ASSERT(r1 != NULL);
    ASSERT(r2 != NULL);
    ASSERT_STR_EQ("\x1b[0n", r1); /* Status OK */
    free((void *)r1);
    free((void *)r2);
    /* No more responses */
    ASSERT(wixen_terminal_pop_response(&t) == NULL);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Scroll region reset === */
TEST scroll_region_reset(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[5;15r"); /* Set region */
    ASSERT_EQ(4, (int)t.scroll_region.top);
    ASSERT_EQ(15, (int)t.scroll_region.bottom);
    feed(&t, "\x1b[r"); /* Reset — full screen */
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(24, (int)t.scroll_region.bottom);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Alt screen doesn't affect scrollback === */
TEST alt_screen_isolation(void) {
    WixenTerminal t; wixen_terminal_init(&t, 10, 3); setup();
    feed(&t, "Main");
    feed(&t, "\x1b[?1049h"); /* Enter alt */
    feed(&t, "Alt1\r\nAlt2\r\nAlt3");
    ASSERT_STR_EQ("Alt1", row_text(&t, 0));
    feed(&t, "\x1b[?1049l"); /* Exit alt */
    ASSERT_STR_EQ("Main", row_text(&t, 0));
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Empty ESC sequence === */
TEST empty_csi(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    /* CSI with just final byte — all params default */
    feed(&t, "\x1b[m"); /* SGR with no params = reset */
    ASSERT_FALSE(t.grid.current_attrs.bold);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Bright background colors === */
TEST sgr_bright_bg(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[101mX"); /* Bright red bg */
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.bg.type);
    ASSERT_EQ(9, c->attrs.bg.index); /* 101-100+8 = 9 */
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Inverse + default colors === */
TEST inverse_default_colors(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[7mX"); /* Inverse */
    const WixenCell *c = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT(c->attrs.inverse);
    feed(&t, "\x1b[27mY"); /* Cancel inverse */
    const WixenCell *c2 = wixen_grid_cell_const(&t.grid, 1, 0);
    ASSERT_FALSE(c2->attrs.inverse);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Hidden text === */
TEST sgr_hidden(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[8mX\x1b[28mY");
    const WixenCell *c0 = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT(c0->attrs.hidden);
    const WixenCell *c1 = wixen_grid_cell_const(&t.grid, 1, 0);
    ASSERT_FALSE(c1->attrs.hidden);
    wixen_terminal_free(&t); teardown();
    PASS();
}

/* === Blink text === */
TEST sgr_blink(void) {
    WixenTerminal t; wixen_terminal_init(&t, 80, 24); setup();
    feed(&t, "\x1b[5mX\x1b[25mY");
    const WixenCell *c0 = wixen_grid_cell_const(&t.grid, 0, 0);
    ASSERT(c0->attrs.blink);
    const WixenCell *c1 = wixen_grid_cell_const(&t.grid, 1, 0);
    ASSERT_FALSE(c1->attrs.blink);
    wixen_terminal_free(&t); teardown();
    PASS();
}

SUITE(extended_erase) {
    RUN_TEST(ed_mode_1);
    RUN_TEST(el_mode2_mid_line);
}

SUITE(extended_modes) {
    RUN_TEST(lnm_mode);
    RUN_TEST(decckm_mode);
}

SUITE(extended_sgr) {
    RUN_TEST(sgr_multiple_combined);
    RUN_TEST(sgr_bright_bg);
    RUN_TEST(inverse_default_colors);
    RUN_TEST(sgr_hidden);
    RUN_TEST(sgr_blink);
}

SUITE(extended_cursor) {
    RUN_TEST(cr_at_zero);
    RUN_TEST(bs_at_zero);
    RUN_TEST(cup_large_params);
}

SUITE(extended_misc) {
    RUN_TEST(print_overwrites);
    RUN_TEST(multiple_responses);
    RUN_TEST(scroll_region_reset);
    RUN_TEST(alt_screen_isolation);
    RUN_TEST(empty_csi);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(extended_erase);
    RUN_SUITE(extended_modes);
    RUN_SUITE(extended_sgr);
    RUN_SUITE(extended_cursor);
    RUN_SUITE(extended_misc);
    GREATEST_MAIN_END();
}
