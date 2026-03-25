/* test_red_null_safety.c — RED tests for NULL input robustness
 *
 * C has no option types. Every public function that takes a pointer
 * must handle NULL gracefully — no crash, no UB. This is the #1
 * source of C-specific bugs that Rust prevents at compile time.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/selection.h"
#include "wixen/core/buffer.h"
#include "wixen/core/url.h"
#include "wixen/core/error_detect.h"
#include "wixen/core/hyperlink.h"
#include "wixen/a11y/events.h"
#include "wixen/search/search.h"
#include "wixen/config/config.h"
#include "wixen/config/keybindings.h"

/* === Terminal NULL inputs === */

TEST null_terminal_dispatch(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    WixenAction null_action = {0};
    wixen_terminal_dispatch(&t, &null_action);
    /* Should not crash */
    wixen_terminal_free(&t);
    PASS();
}

TEST null_terminal_set_title(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    wixen_terminal_set_title(&t, NULL);
    ASSERT(t.title == NULL);
    wixen_terminal_free(&t);
    PASS();
}

TEST null_terminal_extract_row(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    char *text = wixen_terminal_extract_row_text(&t, 999);
    ASSERT(text != NULL);
    ASSERT_EQ(0, (int)strlen(text));
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

TEST null_terminal_selected_text(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    char *text = wixen_terminal_selected_text(&t, NULL);
    ASSERT(text != NULL);
    ASSERT_EQ(0, (int)strlen(text));
    free(text);
    wixen_terminal_free(&t);
    PASS();
}

TEST null_terminal_hyperlink_at(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    const char *url = wixen_terminal_hyperlink_at(&t, 999, 999);
    ASSERT(url == NULL);
    wixen_terminal_free(&t);
    PASS();
}

TEST null_terminal_pop_response_empty(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp == NULL);
    wixen_terminal_free(&t);
    PASS();
}

TEST null_terminal_drain_clipboard_empty(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    char *clip = wixen_terminal_drain_clipboard_write(&t);
    ASSERT(clip != NULL); /* Returns empty string, not NULL */
    free(clip);
    wixen_terminal_free(&t);
    PASS();
}

/* === Grid NULL inputs === */

TEST null_grid_cell_oob(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    WixenCell *cell = wixen_grid_cell(&g, 999, 999);
    ASSERT(cell == NULL);
    wixen_grid_free(&g);
    PASS();
}

/* === A11y NULL inputs === */

TEST null_strip_vt(void) {
    char *s = wixen_strip_vt_escapes(NULL);
    ASSERT(s != NULL);
    ASSERT_EQ(0, (int)strlen(s));
    free(s);
    PASS();
}

TEST null_strip_control(void) {
    char *s = wixen_strip_control_chars(NULL);
    ASSERT(s != NULL);
    ASSERT_EQ(0, (int)strlen(s));
    free(s);
    PASS();
}

TEST null_format_command_complete(void) {
    char *s = wixen_a11y_format_command_complete(NULL, 0);
    ASSERT(s != NULL);
    free(s);
    PASS();
}

TEST null_format_mode_change(void) {
    char *s = wixen_a11y_format_mode_change("zoom", true);
    ASSERT(s != NULL);
    free(s);
    s = wixen_a11y_format_image_placed(10, 10, NULL);
    ASSERT(s != NULL);
    free(s);
    PASS();
}

/* === URL NULL inputs === */

TEST null_url_detect(void) {
    WixenUrlMatch *m = NULL;
    size_t count = wixen_detect_urls(NULL, &m);
    ASSERT_EQ(0, (int)count);
    PASS();
}

TEST null_url_scheme(void) {
    ASSERT_FALSE(wixen_is_safe_url_scheme(NULL));
    PASS();
}

/* === Error detect NULL === */

TEST null_error_classify(void) {
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line(NULL));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line(""));
    PASS();
}

/* === Search NULL === */

TEST null_search_execute(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    WixenSearchOptions opts = {false, false, true};
    wixen_search_execute(&se, NULL, opts, NULL, 0, 0, 0);
    ASSERT_EQ(0, (int)wixen_search_match_count(&se));
    wixen_search_free(&se);
    PASS();
}

/* === Config NULL === */

TEST null_keybinding_lookup(void) {
    WixenKeybindingMap kb;
    wixen_keybindings_init(&kb);
    const char *action = wixen_keybindings_lookup(&kb, NULL);
    ASSERT(action == NULL);
    action = wixen_keybindings_lookup(&kb, "");
    ASSERT(action == NULL);
    wixen_keybindings_free(&kb);
    PASS();
}

/* === Base64 NULL === */

TEST null_base64_encode(void) {
    char *s = wixen_base64_encode(NULL, 0);
    ASSERT(s != NULL);
    ASSERT_EQ(0, (int)strlen(s));
    free(s);
    PASS();
}

TEST null_base64_decode(void) {
    size_t out_len = 99;
    uint8_t *d = wixen_base64_decode(NULL, &out_len);
    /* Should handle gracefully */
    ASSERT(d == NULL || out_len == 0);
    free(d);
    PASS();
}

/* === Double free safety === */

TEST double_free_terminal(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 5);
    wixen_terminal_free(&t);
    /* Second free should not crash (pointers should be NULL) */
    wixen_terminal_free(&t);
    PASS();
}

TEST double_free_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 5, 3);
    wixen_grid_free(&g);
    wixen_grid_free(&g);
    PASS();
}

SUITE(null_safety) {
    RUN_TEST(null_terminal_dispatch);
    RUN_TEST(null_terminal_set_title);
    RUN_TEST(null_terminal_extract_row);
    RUN_TEST(null_terminal_selected_text);
    RUN_TEST(null_terminal_hyperlink_at);
    RUN_TEST(null_terminal_pop_response_empty);
    RUN_TEST(null_terminal_drain_clipboard_empty);
    RUN_TEST(null_grid_cell_oob);
    RUN_TEST(null_strip_vt);
    RUN_TEST(null_strip_control);
    RUN_TEST(null_format_command_complete);
    RUN_TEST(null_format_mode_change);
    RUN_TEST(null_url_detect);
    RUN_TEST(null_url_scheme);
    RUN_TEST(null_error_classify);
    RUN_TEST(null_search_execute);
    RUN_TEST(null_keybinding_lookup);
    RUN_TEST(null_base64_encode);
    RUN_TEST(null_base64_decode);
    RUN_TEST(double_free_terminal);
    RUN_TEST(double_free_grid);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(null_safety);
    GREATEST_MAIN_END();
}
