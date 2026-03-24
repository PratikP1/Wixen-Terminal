/* test_red_shipped_debt.c — RED tests for features shipped without TDD
 *
 * These test features that were implemented without failing tests first.
 * Writing them retroactively to verify correctness and catch bugs.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/selection.h"
#include "wixen/core/hyperlink.h"
#include "wixen/core/error_detect.h"
#include "wixen/vt/parser.h"
#include "wixen/config/session.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* === DECSTR soft reset (CSI ! p) === */

TEST debt_decstr_resets_modes(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?7l");   /* No autowrap */
    feed(&t, &p, "\x1b[?25l");  /* Hide cursor */
    feed(&t, &p, "\x1b[4h");    /* Insert mode */
    ASSERT_FALSE(t.modes.auto_wrap);
    ASSERT_FALSE(t.modes.cursor_visible);
    ASSERT(t.modes.insert_mode);
    feed(&t, &p, "\x1b[!p");    /* DECSTR */
    ASSERT(t.modes.auto_wrap);
    ASSERT(t.modes.cursor_visible);
    /* Insert mode should be off after reset */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST debt_decstr_resets_scroll_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;7r");
    ASSERT_EQ(2, (int)t.scroll_region.top);
    feed(&t, &p, "\x1b[!p");
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(10, (int)t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST debt_decstr_homes_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5;10H");
    feed(&t, &p, "\x1b[!p");
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Box drawing charset (ESC ( 0) === */

TEST debt_box_drawing_vertical(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b(0"); /* Switch to DEC Special Graphics */
    feed(&t, &p, "x");      /* 'x' → │ (U+2502) */
    /* Cell should contain the Unicode box drawing char, not ASCII 'x' */
    ASSERT(strcmp(t.grid.rows[0].cells[0].content, "x") != 0);
    ASSERT(strlen(t.grid.rows[0].cells[0].content) > 1); /* UTF-8 multi-byte */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST debt_box_drawing_horizontal(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b(0"); /* DEC Special */
    feed(&t, &p, "q");      /* 'q' → ─ (U+2500) */
    ASSERT(strcmp(t.grid.rows[0].cells[0].content, "q") != 0);
    feed(&t, &p, "\x1b(B"); /* Back to ASCII */
    feed(&t, &p, "q");      /* Now 'q' should be literal 'q' */
    ASSERT_STR_EQ("q", t.grid.rows[0].cells[1].content);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST debt_box_drawing_corner(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b(0");
    feed(&t, &p, "l"); /* 'l' → ┌ (U+250C) */
    feed(&t, &p, "k"); /* 'k' → ┐ (U+2510) */
    feed(&t, &p, "m"); /* 'm' → └ (U+2514) */
    feed(&t, &p, "j"); /* 'j' → ┘ (U+2518) */
    /* All should be multi-byte UTF-8 */
    for (int i = 0; i < 4; i++) {
        ASSERT(strlen(t.grid.rows[0].cells[i].content) >= 3); /* UTF-8 box = 3 bytes */
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === XTWINOPS (CSI t) === */

TEST debt_xtwinops_report_size(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[18t");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    /* Response should be ESC[8;rows;colst */
    ASSERT(strstr(resp, "8;") != NULL);
    ASSERT(strstr(resp, ";80t") != NULL || strstr(resp, "80t") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === OSC 12 cursor color === */

TEST debt_osc12_cursor_color(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]12;?\x07");
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "rgb:") != NULL);
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Error/warning detection on output === */

TEST debt_error_line_detected(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("error: file not found"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("FATAL: connection refused"));
    ASSERT_EQ(WIXEN_LINE_WARNING, wixen_classify_output_line("warning: unused variable"));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("Hello World"));
    PASS();
}

/* === Block selection === */

TEST debt_block_selection(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 2, WIXEN_SEL_BLOCK);
    wixen_selection_update(&sel, 10, 8);
    /* Block: cols 5-10, rows 2-8 */
    ASSERT(wixen_selection_contains(&sel, 7, 5, 80));  /* Inside */
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 5, 80));  /* Left of block */
    ASSERT_FALSE(wixen_selection_contains(&sel, 11, 5, 80)); /* Right of block */
    ASSERT_FALSE(wixen_selection_contains(&sel, 7, 1, 80));  /* Above block */
    ASSERT_FALSE(wixen_selection_contains(&sel, 7, 9, 80));  /* Below block */
    PASS();
}

/* === Line selection (triple-click) === */

TEST debt_line_selection(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 5, WIXEN_SEL_LINE);
    wixen_selection_update(&sel, 79, 5);
    ASSERT(wixen_selection_contains(&sel, 0, 5, 80));
    ASSERT(wixen_selection_contains(&sel, 79, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 4, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 0, 6, 80));
    PASS();
}

/* === Session save/restore round-trip === */

TEST debt_session_roundtrip_with_cwd(void) {
    WixenSessionState ss;
    wixen_session_init(&ss);
    wixen_session_add_tab(&ss, "Dev", "pwsh.exe", "C:\\Projects\\Wixen");
    wixen_session_add_tab(&ss, "Build", "cmd.exe", "C:\\Build");

    const char *path = "test_debt_session.json";
    ASSERT(wixen_session_save(&ss, path));

    WixenSessionState loaded;
    wixen_session_init(&loaded);
    ASSERT(wixen_session_load(&loaded, path));
    ASSERT_EQ(2, (int)loaded.tab_count);
    ASSERT_STR_EQ("Dev", loaded.tabs[0].title);
    ASSERT_STR_EQ("C:\\Projects\\Wixen", loaded.tabs[0].working_directory);
    ASSERT_STR_EQ("Build", loaded.tabs[1].title);

    wixen_session_free(&loaded);
    wixen_session_free(&ss);
    remove(path);
    PASS();
}

/* === Application keypad mode === */

TEST debt_keypad_app_mode(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    ASSERT_FALSE(t.modes.keypad_application);
    feed(&t, &p, "\x1b="); /* DECKPAM */
    ASSERT(t.modes.keypad_application);
    feed(&t, &p, "\x1b>"); /* DECKPNM */
    ASSERT_FALSE(t.modes.keypad_application);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Charset switching === */

TEST debt_charset_so_si(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    ASSERT_EQ(0, t.active_charset);
    feed(&t, &p, "\x0E"); /* SO: shift to G1 */
    ASSERT_EQ(1, t.active_charset);
    feed(&t, &p, "\x0F"); /* SI: shift to G0 */
    ASSERT_EQ(0, t.active_charset);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_shipped_debt) {
    /* DECSTR */
    RUN_TEST(debt_decstr_resets_modes);
    RUN_TEST(debt_decstr_resets_scroll_region);
    RUN_TEST(debt_decstr_homes_cursor);
    /* Box drawing */
    RUN_TEST(debt_box_drawing_vertical);
    RUN_TEST(debt_box_drawing_horizontal);
    RUN_TEST(debt_box_drawing_corner);
    /* XTWINOPS */
    RUN_TEST(debt_xtwinops_report_size);
    /* OSC 12 */
    RUN_TEST(debt_osc12_cursor_color);
    /* Error detection */
    RUN_TEST(debt_error_line_detected);
    /* Selection types */
    RUN_TEST(debt_block_selection);
    RUN_TEST(debt_line_selection);
    /* Session */
    RUN_TEST(debt_session_roundtrip_with_cwd);
    /* Keypad + charset */
    RUN_TEST(debt_keypad_app_mode);
    RUN_TEST(debt_charset_so_si);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_shipped_debt);
    GREATEST_MAIN_END();
}
