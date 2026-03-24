/* test_red_a11y_consistency.c — Verify a11y text matches grid state */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/vt/parser.h"
#include "wixen/a11y/events.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* Grid visible text should contain all printed content */
TEST a11y_visible_text_after_write(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello World");
    char buf[1024];
    wixen_grid_visible_text(&t.grid, buf, sizeof(buf));
    ASSERT(strstr(buf, "Hello World") != NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* After erase, visible text should not contain erased content */
TEST a11y_visible_text_after_erase(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Secret Data");
    feed(&t, &p, "\x1b[2J\x1b[H"); /* Clear screen */
    feed(&t, &p, "Clean State");
    char buf[1024];
    wixen_grid_visible_text(&t.grid, buf, sizeof(buf));
    ASSERT(strstr(buf, "Secret") == NULL);
    ASSERT(strstr(buf, "Clean State") != NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Strip VT escapes from text for screen reader */
TEST a11y_strip_for_sr(void) {
    char *s = wixen_strip_vt_escapes("\x1b[1;31mError:\x1b[0m file not found");
    ASSERT_STR_EQ("Error: file not found", s);
    free(s);
    PASS();
}

/* Strip control chars from notification text */
TEST a11y_strip_ctrl_for_notification(void) {
    char *s = wixen_strip_control_chars("Line\x08" "with\x07" "bells");
    ASSERT(strstr(s, "\x07") == NULL); /* No BEL */
    ASSERT(strstr(s, "\x08") == NULL); /* No BS */
    ASSERT(strstr(s, "Line") != NULL);
    ASSERT(strstr(s, "with") != NULL);
    free(s);
    PASS();
}

/* Visible text after scroll should show current content */
TEST a11y_visible_after_scroll(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "Line1\r\nLine2\r\nLine3\r\nLine4\r\n");
    char buf[1024];
    wixen_grid_visible_text(&t.grid, buf, sizeof(buf));
    /* Line1 should have scrolled off */
    ASSERT(strstr(buf, "Line1") == NULL);
    /* Line2+ should be visible */
    ASSERT(strstr(buf, "Line2") != NULL || strstr(buf, "Line3") != NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Visible text with colored content strips SGR for text */
TEST a11y_colored_content_text(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[31mRed\x1b[32mGreen\x1b[0mNormal");
    char buf[1024];
    wixen_grid_visible_text(&t.grid, buf, sizeof(buf));
    /* Visible text should be plain: "RedGreenNormal" */
    ASSERT(strstr(buf, "Red") != NULL);
    ASSERT(strstr(buf, "Green") != NULL);
    ASSERT(strstr(buf, "Normal") != NULL);
    /* Should NOT contain escape sequences */
    ASSERT(strstr(buf, "\x1b") == NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Row text extraction trims trailing spaces */
TEST a11y_row_text_trimmed(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Short");
    char buf[256];
    size_t len = wixen_row_text(&t.grid.rows[0], buf, sizeof(buf));
    ASSERT_EQ(5, (int)len);
    ASSERT_STR_EQ("Short", buf);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_a11y_consistency) {
    RUN_TEST(a11y_visible_text_after_write);
    RUN_TEST(a11y_visible_text_after_erase);
    RUN_TEST(a11y_strip_for_sr);
    RUN_TEST(a11y_strip_ctrl_for_notification);
    RUN_TEST(a11y_visible_after_scroll);
    RUN_TEST(a11y_colored_content_text);
    RUN_TEST(a11y_row_text_trimmed);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_a11y_consistency);
    GREATEST_MAIN_END();
}
