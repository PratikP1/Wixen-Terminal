/* test_red_prompt_jump.c — RED tests for prompt navigation
 *
 * Users should be able to jump between shell prompts (OSC 133 markers).
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/shell_integ/shell_integ.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

TEST red_jump_next_prompt(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    /* Simulate two prompts with commands and output between them */
    feed(&t, &p, "\x1b]133;A\x07$ ls\r\n");        /* Prompt 1 at row 0 */
    feed(&t, &p, "\x1b]133;C\x07");                  /* Command executed */
    feed(&t, &p, "file1.txt\r\nfile2.txt\r\n");      /* Output */
    feed(&t, &p, "\x1b]133;D;0\x07");                /* Command done, exit 0 */
    feed(&t, &p, "\x1b]133;A\x07$ whoami\r\n");      /* Prompt 2 at row ~4 */
    feed(&t, &p, "\x1b]133;C\x07");
    feed(&t, &p, "pratik\r\n");
    feed(&t, &p, "\x1b]133;D;0\x07");
    feed(&t, &p, "\x1b]133;A\x07$ ");                /* Prompt 3 (current) */

    /* Cursor should be at latest prompt. Jump to previous. */
    size_t before_row = t.grid.cursor.row;
    bool found = wixen_terminal_jump_to_previous_prompt(&t);
    ASSERT(found);
    ASSERT(t.grid.cursor.row < before_row);

    /* Jump to previous again */
    before_row = t.grid.cursor.row;
    found = wixen_terminal_jump_to_previous_prompt(&t);
    ASSERT(found);
    ASSERT(t.grid.cursor.row < before_row);

    /* Jump forward */
    before_row = t.grid.cursor.row;
    found = wixen_terminal_jump_to_next_prompt(&t);
    ASSERT(found);
    ASSERT(t.grid.cursor.row > before_row);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_jump_no_prompts(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 40, 10);
    /* No prompts — jump should return false */
    ASSERT_FALSE(wixen_terminal_jump_to_previous_prompt(&t));
    ASSERT_FALSE(wixen_terminal_jump_to_next_prompt(&t));
    wixen_terminal_free(&t);
    PASS();
}

SUITE(red_prompt_jump) {
    RUN_TEST(red_jump_next_prompt);
    RUN_TEST(red_jump_no_prompts);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_prompt_jump);
    GREATEST_MAIN_END();
}
