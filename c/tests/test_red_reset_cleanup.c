/* test_red_reset_cleanup.c — RED tests for reset cleaning up all state
 *
 * Both RIS (ESC c) and DECSTR (CSI ! p) should clean up hyperlinks,
 * clipboard pending, echo timeout, shell integ, and selection.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_ris_clears_hyperlink(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* Open a hyperlink */
    feed(&t, &p, "\x1b]8;;https://test.com\x07" "A" "\x1b]8;;\x07");
    ASSERT_EQ(0, (int)t.current_hyperlink_id); /* Closed */
    ASSERT(t.hyperlinks.count > 1); /* Store has entries */
    /* RIS should clear hyperlink store */
    feed(&t, &p, "\x1b" "c");
    ASSERT_EQ(0, (int)t.current_hyperlink_id);
    /* Hyperlinks store should be reset (count back to 1 sentinel) */
    ASSERT_EQ(1, (int)t.hyperlinks.count);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ris_clears_selection(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    wixen_terminal_select_all(&t);
    ASSERT(t.selection.active);
    feed(&t, &p, "\x1b" "c");
    ASSERT_FALSE(t.selection.active);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ris_clears_clipboard_pending(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    /* Queue a clipboard write */
    feed(&t, &p, "\x1b]52;c;SGVsbG8=\x07");
    ASSERT(t.clipboard_write_pending != NULL);
    /* RIS should clear it */
    feed(&t, &p, "\x1b" "c");
    ASSERT(t.clipboard_write_pending == NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ris_clears_echo_timeout(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    wixen_terminal_on_char_sent(&t, 'a');
    ASSERT(t.char_sent_pending);
    feed(&t, &p, "\x1b" "c");
    ASSERT_FALSE(t.char_sent_pending);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_ris_clears_title(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b]0;My Title\x07");
    ASSERT(t.title != NULL);
    feed(&t, &p, "\x1b" "c");
    /* Title should be cleared or reset to default */
    ASSERT(t.title == NULL || strlen(t.title) == 0);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_reset_cleanup) {
    RUN_TEST(red_ris_clears_hyperlink);
    RUN_TEST(red_ris_clears_selection);
    RUN_TEST(red_ris_clears_clipboard_pending);
    RUN_TEST(red_ris_clears_echo_timeout);
    RUN_TEST(red_ris_clears_title);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_reset_cleanup);
    GREATEST_MAIN_END();
}
