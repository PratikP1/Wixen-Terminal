/* test_terminal_misc.c — Miscellaneous terminal tests */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[256];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 256);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST term_init_defaults(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    ASSERT_EQ(80, (int)t.grid.cols);
    ASSERT_EQ(24, (int)t.grid.num_rows);
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    ASSERT(t.modes.auto_wrap);
    ASSERT(t.modes.cursor_visible);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_dirty_on_write(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    t.dirty = false;
    feed(&t, &p, "A");
    ASSERT(t.dirty);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST term_title_null_initially(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT(t.title == NULL || strlen(t.title) == 0);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_free_safe(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    wixen_terminal_free(&t);
    /* Second free should be safe */
    wixen_terminal_free(&t);
    PASS();
}

TEST term_resize_preserves_content(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Hello");
    wixen_terminal_resize(&t, 20, 10);
    ASSERT_EQ(20, (int)t.grid.cols);
    ASSERT_EQ(10, (int)t.grid.num_rows);
    ASSERT_STR_EQ("H", t.grid.rows[0].cells[0].content);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST term_resize_smaller(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "Test content");
    wixen_terminal_resize(&t, 5, 3);
    ASSERT_EQ(5, (int)t.grid.cols);
    ASSERT_EQ(3, (int)t.grid.num_rows);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST term_scroll_region_default(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(5, (int)t.scroll_region.bottom);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_bell_default_false(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_FALSE(t.bell_pending);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_pending_wrap_default_false(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_FALSE(t.pending_wrap);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_no_alt_screen_initially(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_FALSE(t.modes.alternate_screen);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_cursor_visible_default(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT(t.modes.cursor_visible);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_no_bracketed_paste_default(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_no_cursor_keys_app_default(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_FALSE(t.modes.cursor_keys_application);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_auto_wrap_default_on(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT(t.modes.auto_wrap);
    wixen_terminal_free(&t);
    PASS();
}

TEST term_selection_not_active(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    ASSERT_FALSE(t.selection.active);
    wixen_terminal_free(&t);
    PASS();
}

SUITE(terminal_misc) {
    RUN_TEST(term_init_defaults);
    RUN_TEST(term_dirty_on_write);
    RUN_TEST(term_title_null_initially);
    RUN_TEST(term_free_safe);
    RUN_TEST(term_resize_preserves_content);
    RUN_TEST(term_resize_smaller);
    RUN_TEST(term_scroll_region_default);
    RUN_TEST(term_bell_default_false);
    RUN_TEST(term_pending_wrap_default_false);
    RUN_TEST(term_no_alt_screen_initially);
    RUN_TEST(term_cursor_visible_default);
    RUN_TEST(term_no_bracketed_paste_default);
    RUN_TEST(term_no_cursor_keys_app_default);
    RUN_TEST(term_auto_wrap_default_on);
    RUN_TEST(term_selection_not_active);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(terminal_misc);
    GREATEST_MAIN_END();
}
