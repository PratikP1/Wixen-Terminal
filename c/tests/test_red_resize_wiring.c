/* test_red_resize_wiring.c — RED tests: resize path must use reflow
 *
 * The app resize handler should call wixen_terminal_resize_reflow when NOT
 * in alternate screen, and plain wixen_terminal_resize when IN alt screen.
 * These tests encode that contract by simulating the decision main.c must make.
 */
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

/* Simulate the resize path that main.c SHOULD use:
 * - alt screen  => wixen_terminal_resize  (plain, no reflow)
 * - normal      => wixen_terminal_resize_reflow
 */
static void app_resize(WixenTerminal *t, size_t new_cols, size_t new_rows) {
    if (t->modes.alternate_screen) {
        wixen_terminal_resize(t, new_cols, new_rows);
    } else {
        wixen_terminal_resize_reflow(t, new_cols, new_rows);
    }
}

/* ---- Test 1: Normal screen resize uses reflow (text wraps correctly) ---- */

TEST resize_normal_screen_reflows_text(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);

    /* Write 10 chars that fill row 0 exactly */
    feed(&t, &p, "ABCDEFGHIJ");

    /* Confirm not in alt screen */
    ASSERT_FALSE(t.modes.alternate_screen);

    /* Resize narrower via the app path — should reflow */
    app_resize(&t, 5, 5);

    /* After reflow, 10 chars should wrap into two 5-char rows */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);
    ASSERT_STR_EQ("ABCDE", r0);
    ASSERT_STR_EQ("FGHIJ", r1);
    free(r0); free(r1);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* ---- Test 2: Alt screen resize does NOT reflow ---- */

TEST resize_alt_screen_no_reflow(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);

    /* Write text in normal screen, then enter alt screen and write there */
    feed(&t, &p, "NormalText");
    wixen_terminal_enter_alt_screen(&t);
    ASSERT(t.modes.alternate_screen);

    /* Write 10 chars on alt screen */
    feed(&t, &p, "ALTSCREEN!");

    /* Resize narrower via the app path — alt screen uses plain resize, no reflow */
    app_resize(&t, 5, 5);

    /* Plain resize truncates/clips — row 0 should NOT have the full 10-char
     * text reflowed into two rows. With plain resize the grid just gets
     * resized and content beyond the new width is clipped. */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    char *r1 = wixen_terminal_extract_row_text(&t, 1);

    /* Row 0 should be at most 5 chars (truncated, not reflowed) */
    ASSERT(strlen(r0) <= 5);

    /* Row 1 should NOT contain the continuation "EEN!" that reflow would put there */
    /* With plain resize, row 1 is whatever was on row 1 before (empty or other content) */
    if (r1) {
        /* If reflow happened, r1 would contain "REEN!" — verify it does not */
        ASSERT(strstr(r1, "REEN!") == NULL);
    }

    free(r0); free(r1);

    wixen_terminal_exit_alt_screen(&t);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* ---- Test 3: Cursor position preserved/adjusted after reflow resize ---- */

TEST resize_reflow_preserves_cursor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);

    /* Write 10 chars — cursor ends at row 0, col 10 (past last char) */
    feed(&t, &p, "ABCDEFGHIJ");
    ASSERT_FALSE(t.modes.alternate_screen);

    /* Resize narrower — reflow splits into 2 rows */
    app_resize(&t, 5, 5);

    /* Cursor should be adjusted to follow the text.
     * Original cursor was at logical position 10 (end of "ABCDEFGHIJ").
     * After reflow to width 5: row 1, col 5 (or clamped to col 4 = last col).
     * The cursor should be on row 1 (the second physical row). */
    ASSERT_EQ(1, (int)t.grid.cursor.row);

    /* Cursor col should be at or near column 5 (end of second row).
     * Implementation may clamp to new_cols-1 or keep at new_cols. */
    ASSERT(t.grid.cursor.col <= 5);
    ASSERT(t.grid.cursor.col >= 4);  /* Should be at end of the reflowed line */

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_resize_wiring) {
    RUN_TEST(resize_normal_screen_reflows_text);
    RUN_TEST(resize_alt_screen_no_reflow);
    RUN_TEST(resize_reflow_preserves_cursor);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_resize_wiring);
    GREATEST_MAIN_END();
}
