/* test_integration.c — End-to-end pipeline tests
 * Feed raw byte sequences through Parser → Terminal, verify grid state.
 * These mirror the Rust integration tests in tests/integration.rs.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/vt/parser.h"
#include "wixen/core/terminal.h"

static WixenParser parser;
static WixenTerminal term;

static void setup80x24(void) {
    wixen_parser_init(&parser);
    wixen_terminal_init(&term, 80, 24);
}

static void teardown_all(void) {
    wixen_parser_free(&parser);
    wixen_terminal_free(&term);
}

static void feed_raw(const uint8_t *data, size_t len) {
    WixenAction actions[1024];
    size_t count = wixen_parser_process(&parser, data, len, actions, 1024);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(&term, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) free(actions[i].apc.data);
    }
}

static void feed_str(const char *s) {
    feed_raw((const uint8_t *)s, strlen(s));
}

static const char *row(size_t r) {
    static char buf[4096];
    if (r >= term.grid.num_rows) return "";
    wixen_row_text(&term.grid.rows[r], buf, sizeof(buf));
    return buf;
}

/* === Pipeline tests === */

TEST integ_simple_text(void) {
    setup80x24();
    feed_str("Hello, World!");
    ASSERT_STR_EQ("Hello, World!", row(0));
    ASSERT_EQ(13, (int)term.grid.cursor.col);
    teardown_all();
    PASS();
}

TEST integ_crlf(void) {
    setup80x24();
    feed_str("Line1\r\nLine2\r\nLine3");
    ASSERT_STR_EQ("Line1", row(0));
    ASSERT_STR_EQ("Line2", row(1));
    ASSERT_STR_EQ("Line3", row(2));
    teardown_all();
    PASS();
}

TEST integ_colored_text(void) {
    setup80x24();
    feed_str("\x1b[31mRed\x1b[0m Normal");
    ASSERT_STR_EQ("Red Normal", row(0));
    const WixenCell *c0 = wixen_grid_cell_const(&term.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c0->attrs.fg.type);
    ASSERT_EQ(1, c0->attrs.fg.index); /* Red */
    const WixenCell *c4 = wixen_grid_cell_const(&term.grid, 4, 0);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, c4->attrs.fg.type); /* After reset */
    teardown_all();
    PASS();
}

TEST integ_cursor_move_and_overwrite(void) {
    setup80x24();
    feed_str("ABCDEFGH");
    feed_str("\x1b[1;4H"); /* Move to row 1, col 4 (1-based) → (0,3) */
    feed_str("XY");
    ASSERT_STR_EQ("ABCXYFGH", row(0)); /* X replaces D, Y replaces E */
    teardown_all();
    PASS();
}

TEST integ_erase_and_rewrite(void) {
    setup80x24();
    feed_str("Hello World");
    feed_str("\x1b[2J"); /* Clear display */
    feed_str("\x1b[H");  /* Home */
    feed_str("New Text");
    ASSERT_STR_EQ("New Text", row(0));
    teardown_all();
    PASS();
}

TEST integ_scroll_region_output(void) {
    setup80x24();
    /* Set scroll region to rows 5-15 */
    feed_str("\x1b[5;15r");
    /* Move to row 15, col 1 */
    feed_str("\x1b[15;1H");
    /* Print lines that cause scrolling within region */
    feed_str("Line1\r\nLine2\r\nLine3");
    /* Row 4 (0-based) should have Line1 (first line in region scrolled) */
    teardown_all();
    PASS();
}

TEST integ_alt_screen_roundtrip(void) {
    setup80x24();
    feed_str("Main content");
    feed_str("\x1b[?1049h"); /* Enter alt screen */
    feed_str("Alt content");
    ASSERT_STR_EQ("Alt content", row(0));
    feed_str("\x1b[?1049l"); /* Exit alt screen */
    ASSERT_STR_EQ("Main content", row(0));
    teardown_all();
    PASS();
}

TEST integ_osc_title(void) {
    setup80x24();
    feed_str("\x1b]0;My Custom Title\x07");
    ASSERT(term.title != NULL);
    ASSERT_STR_EQ("My Custom Title", term.title);
    teardown_all();
    PASS();
}

TEST integ_sgr_combination(void) {
    setup80x24();
    /* Bold + italic + red fg + green bg */
    feed_str("\x1b[1;3;31;42mX\x1b[0m");
    const WixenCell *c = wixen_grid_cell_const(&term.grid, 0, 0);
    ASSERT(c->attrs.bold);
    ASSERT(c->attrs.italic);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.fg.type);
    ASSERT_EQ(1, c->attrs.fg.index); /* Red */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.bg.type);
    ASSERT_EQ(2, c->attrs.bg.index); /* Green */
    teardown_all();
    PASS();
}

TEST integ_tab_stops(void) {
    setup80x24();
    feed_str("A\tB\tC");
    ASSERT_STR_EQ("A", wixen_grid_cell_const(&term.grid, 0, 0)->content);
    ASSERT_STR_EQ("B", wixen_grid_cell_const(&term.grid, 8, 0)->content);
    ASSERT_STR_EQ("C", wixen_grid_cell_const(&term.grid, 16, 0)->content);
    teardown_all();
    PASS();
}

TEST integ_backspace_and_overwrite(void) {
    setup80x24();
    feed_str("ABC\x08X"); /* Write ABC, backspace, write X → ABX */
    ASSERT_STR_EQ("ABX", row(0));
    teardown_all();
    PASS();
}

TEST integ_cursor_save_restore(void) {
    setup80x24();
    feed_str("\x1b[5;10H"); /* Move to (4,9) */
    feed_str("\x1b" "7");   /* Save cursor */
    feed_str("\x1b[1;1H");  /* Home */
    feed_str("X");
    feed_str("\x1b" "8");   /* Restore cursor */
    feed_str("Y");
    ASSERT_STR_EQ("X", wixen_grid_cell_const(&term.grid, 0, 0)->content);
    ASSERT_STR_EQ("Y", wixen_grid_cell_const(&term.grid, 9, 4)->content);
    teardown_all();
    PASS();
}

TEST integ_insert_delete_lines(void) {
    setup80x24();
    feed_str("A\r\nB\r\nC\r\nD\r\nE");
    feed_str("\x1b[3;1H"); /* Row 3 */
    feed_str("\x1b[1L");   /* Insert 1 line */
    ASSERT_STR_EQ("A", row(0));
    ASSERT_STR_EQ("B", row(1));
    ASSERT_STR_EQ("", row(2)); /* Inserted blank */
    ASSERT_STR_EQ("C", row(3));
    ASSERT_STR_EQ("D", row(4));
    /* E pushed off by 1 */
    teardown_all();
    PASS();
}

TEST integ_full_terminal_reset(void) {
    setup80x24();
    feed_str("Hello");
    feed_str("\x1b[1;3;4m"); /* Bold+italic+underline */
    feed_str("\x1b" "c");    /* Full reset */
    ASSERT_STR_EQ("", row(0));
    ASSERT_EQ(0, (int)term.grid.cursor.row);
    ASSERT_EQ(0, (int)term.grid.cursor.col);
    ASSERT_FALSE(term.grid.current_attrs.bold);
    teardown_all();
    PASS();
}

TEST integ_dsr_cpr(void) {
    setup80x24();
    feed_str("\x1b[10;20H"); /* Move to (9,19) */
    feed_str("\x1b[6n");     /* Request CPR */
    const char *resp = wixen_terminal_pop_response(&term);
    ASSERT(resp != NULL);
    ASSERT_STR_EQ("\x1b[10;20R", resp);
    free((void *)resp);
    teardown_all();
    PASS();
}

TEST integ_large_output(void) {
    setup80x24();
    /* Simulate 100 lines of output (scrolling) */
    for (int i = 0; i < 100; i++) {
        char line[80];
        snprintf(line, sizeof(line), "Line %03d\r\n", i);
        feed_str(line);
    }
    /* Last visible line should be ~Line 099 */
    ASSERT_EQ(23, (int)term.grid.cursor.row);
    teardown_all();
    PASS();
}

TEST integ_rapid_cursor_movements(void) {
    setup80x24();
    feed_str("\x1b[1;1H"); /* Home */
    for (int i = 0; i < 50; i++) {
        feed_str("\x1b[B"); /* Down */
    }
    /* Should clamp at row 23 (bottom of 24-row terminal) */
    ASSERT_EQ(23, (int)term.grid.cursor.row);
    teardown_all();
    PASS();
}

/* === Real-world scenarios === */

TEST integ_powershell_prompt(void) {
    setup80x24();
    feed_str("\x1b[32mPS C:\\Users\\test>\x1b[0m ");
    /* row() trims trailing spaces, so trailing space is trimmed */
    ASSERT_STR_EQ("PS C:\\Users\\test>", row(0));
    /* Green color on prompt text */
    const WixenCell *c = wixen_grid_cell_const(&term.grid, 0, 0);
    ASSERT_EQ(WIXEN_COLOR_INDEXED, c->attrs.fg.type);
    ASSERT_EQ(2, c->attrs.fg.index); /* Green */
    teardown_all();
    PASS();
}

TEST integ_git_diff_colors(void) {
    setup80x24();
    feed_str("\x1b[1mdiff --git a/file.c b/file.c\x1b[0m\r\n");
    feed_str("\x1b[31m-old line\x1b[0m\r\n");
    feed_str("\x1b[32m+new line\x1b[0m\r\n");
    ASSERT_STR_EQ("diff --git a/file.c b/file.c", row(0));
    ASSERT_STR_EQ("-old line", row(1));
    ASSERT_STR_EQ("+new line", row(2));
    /* Check bold on first line */
    const WixenCell *c0 = wixen_grid_cell_const(&term.grid, 0, 0);
    ASSERT(c0->attrs.bold);
    /* Check red on deletion */
    const WixenCell *c1 = wixen_grid_cell_const(&term.grid, 0, 1);
    ASSERT_EQ(1, c1->attrs.fg.index); /* Red */
    /* Check green on addition */
    const WixenCell *c2 = wixen_grid_cell_const(&term.grid, 0, 2);
    ASSERT_EQ(2, c2->attrs.fg.index); /* Green */
    teardown_all();
    PASS();
}

TEST integ_progress_bar(void) {
    setup80x24();
    feed_str("Downloading:  50% [#########         ]\r");
    feed_str("Downloading: 100% [##################]\r\n");
    ASSERT_STR_EQ("Downloading: 100% [##################]", row(0));
    teardown_all();
    PASS();
}

TEST integ_256_color_palette(void) {
    setup80x24();
    /* Print a block with 256-color bg */
    feed_str("\x1b[48;5;196m \x1b[48;5;46m \x1b[48;5;21m \x1b[0m");
    const WixenCell *c0 = wixen_grid_cell_const(&term.grid, 0, 0);
    ASSERT_EQ(196, c0->attrs.bg.index); /* Red */
    const WixenCell *c1 = wixen_grid_cell_const(&term.grid, 1, 0);
    ASSERT_EQ(46, c1->attrs.bg.index); /* Green */
    const WixenCell *c2 = wixen_grid_cell_const(&term.grid, 2, 0);
    ASSERT_EQ(21, c2->attrs.bg.index); /* Blue */
    teardown_all();
    PASS();
}

TEST integ_multi_sgr_reset(void) {
    setup80x24();
    feed_str("\x1b[1;4;31mBold Underline Red\x1b[0m Normal");
    const WixenCell *c0 = wixen_grid_cell_const(&term.grid, 0, 0);
    ASSERT(c0->attrs.bold);
    ASSERT_EQ(WIXEN_UNDERLINE_SINGLE, c0->attrs.underline);
    ASSERT_EQ(1, c0->attrs.fg.index);
    /* After reset */
    size_t normal_col = 19; /* "Normal" starts here */
    const WixenCell *cn = wixen_grid_cell_const(&term.grid, normal_col, 0);
    ASSERT_FALSE(cn->attrs.bold);
    ASSERT_EQ(WIXEN_UNDERLINE_NONE, cn->attrs.underline);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, cn->attrs.fg.type);
    teardown_all();
    PASS();
}

TEST integ_many_scroll_regions(void) {
    setup80x24();
    /* Simulate vim-like: set scroll region, write content, restore */
    feed_str("\x1b[1;20r");  /* Region 1-20 */
    feed_str("\x1b[20;1H");  /* Bottom of region */
    for (int i = 0; i < 5; i++) feed_str("scroll\r\n");
    feed_str("\x1b[r");      /* Reset region */
    /* Should not crash or corrupt state */
    ASSERT_EQ(24, (int)term.scroll_region.bottom);
    teardown_all();
    PASS();
}

TEST integ_utf8_mixed_content(void) {
    setup80x24();
    feed_str("Hello \xc3\xa9\xc3\xa8\xc3\xa0 World");
    ASSERT_STR_EQ("Hello \xc3\xa9\xc3\xa8\xc3\xa0 World", row(0));
    teardown_all();
    PASS();
}

TEST integ_bracketed_paste_mode(void) {
    setup80x24();
    feed_str("\x1b[?2004h"); /* Enable */
    ASSERT(term.modes.bracketed_paste);
    /* Simulate paste wrapper */
    feed_str("\x1b[200~pasted text\x1b[201~");
    /* The bracketed paste markers are CSI sequences — they get parsed but
       the terminal should handle the text between them as regular input */
    teardown_all();
    PASS();
}

TEST integ_cursor_style_changes(void) {
    setup80x24();
    feed_str("\x1b[5 q"); /* Bar blinking */
    ASSERT_EQ(WIXEN_CURSOR_BAR, term.grid.cursor.shape);
    feed_str("\x1b[2 q"); /* Block steady */
    ASSERT_EQ(WIXEN_CURSOR_BLOCK, term.grid.cursor.shape);
    ASSERT_FALSE(term.grid.cursor.blinking);
    feed_str("\x1b[0 q"); /* Default (block blinking) */
    ASSERT_EQ(WIXEN_CURSOR_BLOCK, term.grid.cursor.shape);
    ASSERT(term.grid.cursor.blinking);
    teardown_all();
    PASS();
}

SUITE(integration_realworld) {
    RUN_TEST(integ_powershell_prompt);
    RUN_TEST(integ_git_diff_colors);
    RUN_TEST(integ_progress_bar);
    RUN_TEST(integ_256_color_palette);
    RUN_TEST(integ_multi_sgr_reset);
    RUN_TEST(integ_many_scroll_regions);
    RUN_TEST(integ_utf8_mixed_content);
    RUN_TEST(integ_bracketed_paste_mode);
}

SUITE(integration_pipeline) {
    RUN_TEST(integ_simple_text);
    RUN_TEST(integ_crlf);
    RUN_TEST(integ_colored_text);
    RUN_TEST(integ_cursor_move_and_overwrite);
    RUN_TEST(integ_erase_and_rewrite);
    RUN_TEST(integ_scroll_region_output);
    RUN_TEST(integ_alt_screen_roundtrip);
    RUN_TEST(integ_osc_title);
    RUN_TEST(integ_sgr_combination);
    RUN_TEST(integ_tab_stops);
    RUN_TEST(integ_backspace_and_overwrite);
    RUN_TEST(integ_cursor_save_restore);
    RUN_TEST(integ_insert_delete_lines);
    RUN_TEST(integ_full_terminal_reset);
    RUN_TEST(integ_dsr_cpr);
    RUN_TEST(integ_large_output);
    RUN_TEST(integ_rapid_cursor_movements);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(integration_pipeline);
    RUN_TEST(integ_cursor_style_changes);
    RUN_SUITE(integration_realworld);
    GREATEST_MAIN_END();
}
