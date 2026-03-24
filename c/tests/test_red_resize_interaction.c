/* test_red_resize_interaction.c — RED tests for resize interactions with other state */
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

/* Resize should clamp scroll region to new size */
TEST red_resize_clamps_scroll_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;8r"); /* Region rows 3-8 (0-idx: 2-8) */
    ASSERT_EQ(2, (int)t.scroll_region.top);
    ASSERT_EQ(8, (int)t.scroll_region.bottom);
    wixen_terminal_resize(&t, 20, 5); /* Shrink to 5 rows */
    /* Scroll region must be valid within new grid */
    ASSERT(t.scroll_region.bottom <= 5);
    ASSERT(t.scroll_region.top < t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Resize should clamp cursor position */
TEST red_resize_clamps_cursor_pos(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[8;15H"); /* Row 8, col 15 */
    wixen_terminal_resize(&t, 5, 3);
    ASSERT(t.grid.cursor.row < 3);
    ASSERT(t.grid.cursor.col < 5);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Resize preserves tab stops for shared columns */
TEST red_resize_preserves_tabs(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    /* Default tabs at 8,16,24,32 */
    wixen_terminal_resize(&t, 20, 5);
    /* Tab at col 8 and 16 should still exist */
    feed(&t, &p, "\t");
    ASSERT_EQ(8, (int)t.grid.cursor.col);
    feed(&t, &p, "\t");
    ASSERT_EQ(16, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Resize to larger adds new blank rows at bottom */
TEST red_resize_grow_adds_blank_rows(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "AAA\r\nBBB\r\nCCC");
    wixen_terminal_resize(&t, 10, 5);
    ASSERT_EQ(5, (int)t.grid.num_rows);
    /* Original content on rows 0-2 */
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", t.grid.rows[1].cells[0].content);
    ASSERT_STR_EQ("C", t.grid.rows[2].cells[0].content);
    /* Rows 3-4 blank */
    ASSERT(t.grid.rows[3].cells[0].content[0] == '\0' ||
           t.grid.rows[3].cells[0].content[0] == ' ');
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Resize to narrower preserves content that fits */
TEST red_resize_narrow_preserves(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "ABCDE");
    wixen_terminal_resize(&t, 3, 3);
    /* First 3 chars should be preserved */
    ASSERT_STR_EQ("A", t.grid.rows[0].cells[0].content);
    ASSERT_STR_EQ("B", t.grid.rows[0].cells[1].content);
    ASSERT_STR_EQ("C", t.grid.rows[0].cells[2].content);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Multiple rapid resizes shouldn't leak or corrupt */
TEST red_rapid_resize(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feed(&t, &p, "Test content here");
    for (int i = 0; i < 50; i++) {
        wixen_terminal_resize(&t, (size_t)(10 + (i % 70)), (size_t)(3 + (i % 20)));
    }
    /* Should not crash or leak */
    ASSERT(t.grid.cols > 0);
    ASSERT(t.grid.num_rows > 0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* RIS (ESC c) resets scroll region */
TEST red_ris_resets_scroll_region(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[3;8r");
    ASSERT_EQ(2, (int)t.scroll_region.top);
    feed(&t, &p, "\x1b" "c"); /* RIS */
    ASSERT_EQ(0, (int)t.scroll_region.top);
    ASSERT_EQ(10, (int)t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* RIS clears hyperlink state */
TEST red_ris_clears_hyperlinks(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]8;id=test;https://example.com\x07");
    ASSERT(t.current_hyperlink_id != 0);
    feed(&t, &p, "\x1b" "c"); /* RIS */
    ASSERT_EQ(0u, t.current_hyperlink_id);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* RIS resets modes */
TEST red_ris_resets_modes(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 10);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?7l");    /* No autowrap */
    feed(&t, &p, "\x1b[?25l");   /* Hide cursor */
    feed(&t, &p, "\x1b[?1h");    /* App cursor keys */
    feed(&t, &p, "\x1b[?2004h"); /* Bracketed paste */
    feed(&t, &p, "\x1b" "c");    /* RIS */
    ASSERT(t.modes.auto_wrap);
    ASSERT(t.modes.cursor_visible);
    ASSERT_FALSE(t.modes.cursor_keys_application);
    ASSERT_FALSE(t.modes.bracketed_paste);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_resize_interaction) {
    RUN_TEST(red_resize_clamps_scroll_region);
    RUN_TEST(red_resize_clamps_cursor_pos);
    RUN_TEST(red_resize_preserves_tabs);
    RUN_TEST(red_resize_grow_adds_blank_rows);
    RUN_TEST(red_resize_narrow_preserves);
    RUN_TEST(red_rapid_resize);
    RUN_TEST(red_ris_resets_scroll_region);
    RUN_TEST(red_ris_clears_hyperlinks);
    RUN_TEST(red_ris_resets_modes);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_resize_interaction);
    GREATEST_MAIN_END();
}
