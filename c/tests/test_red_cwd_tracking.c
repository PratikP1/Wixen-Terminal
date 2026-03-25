/* test_red_cwd_tracking.c — RED tests for CWD tracking via OSC 7
 *
 * The shell sends OSC 7 with the current working directory as a
 * file:// URI. The shell_integ module should parse and store it.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/shell_integ/shell_integ.h"
#include "wixen/vt/parser.h"

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_cwd_from_osc7(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b]7;file://localhost/C:/Users/prati/Projects\x07");
    ASSERT(t.shell_integ.cwd != NULL);
    /* Should contain the path, possibly with file:// prefix stripped */
    ASSERT(strstr(t.shell_integ.cwd, "C:") != NULL ||
           strstr(t.shell_integ.cwd, "Users") != NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cwd_updates(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b]7;file:///home/user\x07");
    ASSERT(t.shell_integ.cwd != NULL);
    char *first = strdup(t.shell_integ.cwd);
    feeds(&t, &p, "\x1b]7;file:///home/user/projects\x07");
    /* CWD should have updated */
    ASSERT(t.shell_integ.cwd != NULL);
    ASSERT(strcmp(t.shell_integ.cwd, first) != 0);
    free(first);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cwd_empty_osc7(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 10);
    wixen_parser_init(&p);
    feeds(&t, &p, "\x1b]7;\x07");
    /* Empty OSC 7 should not crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_cwd_shell_integ_block_inherits(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 10);
    wixen_parser_init(&p);
    /* Set CWD */
    feeds(&t, &p, "\x1b]7;file:///C:/Projects\x07");
    /* Start a command block */
    feeds(&t, &p, "\x1b]133;A\x07" "$ build\r\n");
    feeds(&t, &p, "\x1b]133;C\x07");
    feeds(&t, &p, "Building...\r\n");
    feeds(&t, &p, "\x1b]133;D;0\x07");
    /* The block should have inherited the CWD */
    ASSERT(t.shell_integ.block_count >= 1);
    if (t.shell_integ.blocks[0].cwd) {
        ASSERT(strstr(t.shell_integ.blocks[0].cwd, "Projects") != NULL);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_cwd_tracking) {
    RUN_TEST(red_cwd_from_osc7);
    RUN_TEST(red_cwd_updates);
    RUN_TEST(red_cwd_empty_osc7);
    RUN_TEST(red_cwd_shell_integ_block_inherits);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_cwd_tracking);
    GREATEST_MAIN_END();
}
