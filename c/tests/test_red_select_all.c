/* test_red_select_all.c — RED tests for select-all and clipboard copy
 *
 * Ctrl+Shift+A should select all visible text.
 * Ctrl+C should copy selection to clipboard queue.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/selection.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_select_all(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "Line0\r\nLine1\r\nLine2");
    wixen_terminal_select_all(&t);
    ASSERT(t.selection.active);
    /* Selection should span entire grid */
    char *text = wixen_terminal_selected_text(&t, &t.selection);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "Line0") != NULL);
    ASSERT(strstr(text, "Line2") != NULL);
    free(text);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_clear_selection(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    wixen_terminal_select_all(&t);
    ASSERT(t.selection.active);
    wixen_terminal_clear_selection(&t);
    ASSERT_FALSE(t.selection.active);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_select_all_empty_grid(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    wixen_terminal_select_all(&t);
    char *text = wixen_terminal_selected_text(&t, &t.selection);
    ASSERT(text != NULL);
    /* Empty grid — selected text should be empty or whitespace-only */
    size_t len = strlen(text);
    bool all_space = true;
    for (size_t i = 0; i < len; i++) {
        if (text[i] != ' ' && text[i] != '\n') { all_space = false; break; }
    }
    ASSERT(len == 0 || all_space);
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

SUITE(red_select_all_suite) {
    RUN_TEST(red_select_all);
    RUN_TEST(red_clear_selection);
    RUN_TEST(red_select_all_empty_grid);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_select_all_suite);
    GREATEST_MAIN_END();
}
