/* test_red_edge_cases.c — RED tests: edge cases that may expose bugs
 *
 * These were written FIRST to find implementation gaps.
 * Any failure here means the implementation needs hardening.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/selection.h"
#include "wixen/core/buffer.h"
#include "wixen/core/hyperlink.h"
#include "wixen/core/url.h"
#include "wixen/vt/parser.h"
#include "wixen/search/search.h"
#include "wixen/config/config.h"
#include "wixen/config/lua_engine.h"
#include "wixen/shell_integ/shell_integ.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[256];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 256);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* --- Terminal edge cases --- */

TEST red_backspace_at_col_zero(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x08"); /* BS at col 0 */
    ASSERT_EQ(0, (int)t.grid.cursor.col); /* Should stay at 0, not underflow */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_massive_cursor_movement(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[99999A"); /* CUU 99999 */
    ASSERT_EQ(0, (int)t.grid.cursor.row);
    feed(&t, &p, "\x1b[99999B"); /* CUD 99999 */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    feed(&t, &p, "\x1b[99999C"); /* CUF 99999 */
    ASSERT_EQ(9, (int)t.grid.cursor.col);
    feed(&t, &p, "\x1b[99999D"); /* CUB 99999 */
    ASSERT_EQ(0, (int)t.grid.cursor.col);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_osc_without_terminator(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* OSC without BEL or ST — parser should handle gracefully */
    feed(&t, &p, "\x1b]0;Unterminated");
    feed(&t, &p, "Normal text"); /* Should recover */
    /* Just checking it doesn't crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_csi_with_too_many_params(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    /* More than 32 params */
    feed(&t, &p, "\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20;21;22;23;24;25;26;27;28;29;30;31;32;33;34m");
    /* Should not crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_invalid_256_index(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[38;5;999mX"); /* Index > 255 */
    /* Should clamp or ignore, not crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_sgr_incomplete_truecolor(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[38;2;255mX"); /* Missing G and B */
    /* Should not crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_scroll_region_inverted(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[5;2r"); /* Top > bottom — invalid */
    /* Should be ignored or clamped */
    ASSERT(t.scroll_region.top < t.scroll_region.bottom);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_alt_screen_double_enter(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[?1049h"); /* Enter alt */
    feed(&t, &p, "\x1b[?1049h"); /* Enter alt again */
    ASSERT(t.modes.alternate_screen);
    feed(&t, &p, "\x1b[?1049l"); /* Leave */
    ASSERT_FALSE(t.modes.alternate_screen);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* --- Grid edge cases --- */

TEST red_grid_resize_to_1x1(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    wixen_cell_set_content(&g.rows[0].cells[0], "X");
    wixen_grid_resize(&g, 1, 1);
    ASSERT_EQ(1, (int)g.cols);
    ASSERT_EQ(1, (int)g.num_rows);
    wixen_grid_free(&g);
    PASS();
}

TEST red_grid_resize_same_size(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    wixen_cell_set_content(&g.rows[0].cells[0], "Y");
    wixen_grid_resize(&g, 10, 5);
    ASSERT_STR_EQ("Y", g.rows[0].cells[0].content);
    wixen_grid_free(&g);
    PASS();
}

/* --- Selection edge cases --- */

TEST red_selection_ordered_no_update(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 5, WIXEN_SEL_NORMAL);
    /* Never called update — end == start */
    WixenGridPoint s, e;
    wixen_selection_ordered(&sel, &s, &e);
    ASSERT_EQ(5, (int)s.col);
    ASSERT_EQ(5, (int)s.row);
    PASS();
}

/* --- Scrollback edge cases --- */

TEST red_scrollback_get_hot_out_of_bounds(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    const WixenRow *r = wixen_scrollback_get_hot(&sb, 999);
    ASSERT(r == NULL);
    wixen_scrollback_free(&sb);
    PASS();
}

/* --- Hyperlink edge cases --- */

TEST red_hyperlink_close_no_active(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    wixen_hyperlinks_close(&hs); /* Close when none open */
    /* Should not crash */
    wixen_hyperlinks_free(&hs);
    PASS();
}

TEST red_hyperlink_get_invalid_id(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    const WixenHyperlink *h = wixen_hyperlinks_get(&hs, 9999);
    ASSERT(h == NULL);
    wixen_hyperlinks_free(&hs);
    PASS();
}

/* --- Search edge cases --- */

TEST red_search_invalid_regex(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    WixenSearchOptions opts = { true, false, true }; /* Regex mode */
    const char *rows[] = { "test" };
    /* Invalid regex should not crash */
    wixen_search_execute(&se, "[invalid(", opts, rows, 1, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

TEST red_search_empty_rows(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    WixenSearchOptions opts = { false, false, true };
    wixen_search_execute(&se, "test", opts, NULL, 0, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

/* --- Shell integration edge cases --- */

TEST red_shell_integ_invalid_marker(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'Z', NULL, 0); /* Invalid marker */
    /* Should not crash, should ignore */
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_shell_integ_d_without_abc(void) {
    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 0); /* D without A/B/C */
    /* Should handle gracefully */
    wixen_shell_integ_free(&si);
    PASS();
}

/* --- Lua edge cases --- */

TEST red_lua_get_string_nil(void) {
    WixenLuaEngine *e = wixen_lua_create();
    char *s = wixen_lua_get_string(e, "nonexistent_var");
    ASSERT(s == NULL);
    wixen_lua_destroy(e);
    PASS();
}

TEST red_lua_exec_runtime_error(void) {
    WixenLuaEngine *e = wixen_lua_create();
    bool ok = wixen_lua_exec_string(e, "error('boom')");
    ASSERT_FALSE(ok);
    /* Engine should still be usable after runtime error */
    ok = wixen_lua_exec_string(e, "x = 42");
    ASSERT(ok);
    wixen_lua_destroy(e);
    PASS();
}

/* --- URL edge cases --- */

TEST red_url_detect_in_empty_string(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls("", &m);
    ASSERT_EQ(0, (int)count);
    PASS();
}

TEST red_url_detect_null_string(void) {
    WixenUrlMatch *m;
    size_t count = wixen_detect_urls(NULL, &m);
    ASSERT_EQ(0, (int)count);
    PASS();
}

/* --- Config edge cases --- */

TEST red_config_save_invalid_path(void) {
    WixenConfig cfg;
    wixen_config_init_defaults(&cfg);
    /* Save to an impossible path */
    bool ok = wixen_config_save(&cfg, "Z:\\nonexistent\\dir\\config.toml");
    ASSERT_FALSE(ok);
    wixen_config_free(&cfg);
    PASS();
}

/* --- Parser edge cases --- */

TEST red_parser_byte_at_a_time(void) {
    WixenParser p;
    wixen_parser_init(&p);
    const char *seq = "\x1b[31;1mHello\x1b[0m";
    size_t len = strlen(seq);
    WixenAction actions[64];
    size_t total = 0;
    for (size_t i = 0; i < len; i++) {
        total += wixen_parser_process(&p, (const uint8_t *)&seq[i], 1, &actions[total], 64 - total);
    }
    ASSERT(total > 0);
    /* Should have PRINT actions for H,e,l,l,o */
    size_t prints = 0;
    for (size_t i = 0; i < total; i++) {
        if (actions[i].type == WIXEN_ACTION_PRINT) prints++;
    }
    ASSERT_EQ(5, (int)prints);
    wixen_parser_free(&p);
    PASS();
}

SUITE(red_edge_cases) {
    /* Terminal */
    RUN_TEST(red_backspace_at_col_zero);
    RUN_TEST(red_massive_cursor_movement);
    RUN_TEST(red_osc_without_terminator);
    RUN_TEST(red_csi_with_too_many_params);
    RUN_TEST(red_sgr_invalid_256_index);
    RUN_TEST(red_sgr_incomplete_truecolor);
    RUN_TEST(red_scroll_region_inverted);
    RUN_TEST(red_alt_screen_double_enter);
    /* Grid */
    RUN_TEST(red_grid_resize_to_1x1);
    RUN_TEST(red_grid_resize_same_size);
    /* Selection */
    RUN_TEST(red_selection_ordered_no_update);
    /* Scrollback */
    RUN_TEST(red_scrollback_get_hot_out_of_bounds);
    /* Hyperlinks */
    RUN_TEST(red_hyperlink_close_no_active);
    RUN_TEST(red_hyperlink_get_invalid_id);
    /* Search */
    RUN_TEST(red_search_invalid_regex);
    RUN_TEST(red_search_empty_rows);
    /* Shell integration */
    RUN_TEST(red_shell_integ_invalid_marker);
    RUN_TEST(red_shell_integ_d_without_abc);
    /* Lua */
    RUN_TEST(red_lua_get_string_nil);
    RUN_TEST(red_lua_exec_runtime_error);
    /* URL */
    RUN_TEST(red_url_detect_in_empty_string);
    RUN_TEST(red_url_detect_null_string);
    /* Config */
    RUN_TEST(red_config_save_invalid_path);
    /* Parser */
    RUN_TEST(red_parser_byte_at_a_time);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_edge_cases);
    GREATEST_MAIN_END();
}
