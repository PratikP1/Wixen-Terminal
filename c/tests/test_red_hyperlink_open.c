/* test_red_hyperlink_open.c — RED tests for hyperlink Ctrl+click
 *
 * When user Ctrl+clicks a cell with a hyperlink, the URL should
 * be extracted and queued for the host to open in a browser.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/hyperlink.h"
#include "wixen/core/url.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_hyperlink_at_cell(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    /* OSC 8 ; ; https://example.com BEL  + text + OSC 8 ;; BEL (close) */
    feed(&t, &p, "\x1b]8;;https://example.com\x07Click me\x1b]8;;\x07");
    /* Cell at (0,0) should have a hyperlink */
    const char *url = wixen_terminal_hyperlink_at(&t, 0, 0);
    ASSERT(url != NULL);
    ASSERT_STR_EQ("https://example.com", url);
    /* Cell outside link range should have no hyperlink */
    const char *no_url = wixen_terminal_hyperlink_at(&t, 20, 0);
    ASSERT(no_url == NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_hyperlink_at_empty(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 20, 5);
    const char *url = wixen_terminal_hyperlink_at(&t, 0, 0);
    ASSERT(url == NULL);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_hyperlink_is_safe(void) {
    ASSERT(wixen_is_safe_url_scheme("https://example.com"));
    ASSERT(wixen_is_safe_url_scheme("http://example.com"));
    ASSERT_FALSE(wixen_is_safe_url_scheme("javascript:alert(1)"));
    ASSERT_FALSE(wixen_is_safe_url_scheme("file:///etc/passwd"));
    PASS();
}

SUITE(red_hyperlink_open) {
    RUN_TEST(red_hyperlink_at_cell);
    RUN_TEST(red_hyperlink_at_empty);
    RUN_TEST(red_hyperlink_is_safe);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_hyperlink_open);
    GREATEST_MAIN_END();
}
