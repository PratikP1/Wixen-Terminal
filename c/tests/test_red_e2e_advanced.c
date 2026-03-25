/* test_red_e2e_advanced.c — RED advanced end-to-end integration
 *
 * Multi-subsystem scenarios: shell integ + text extract + selection,
 * hyperlink + click, alt screen + cursor save, resize during output.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/selection.h"
#include "wixen/a11y/events.h"
#include "wixen/vt/parser.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

static void feeds(WixenTerminal *t, WixenParser *p, const char *s) {
    WixenAction actions[512];
    size_t len = strlen(s);
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 256) chunk = 256;
        size_t count = wixen_parser_process(p, (const uint8_t *)s + pos, chunk, actions, 512);
        for (size_t i = 0; i < count; i++) {
            wixen_terminal_dispatch(t, &actions[i]);
            if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
            if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) free(actions[i].apc.data);
        }
        pos += chunk;
    }
}

/* === Hyperlink in git output → extract URL === */

TEST red_e2e_hyperlink_in_output(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 10);
    wixen_parser_init(&p);

    feeds(&t, &p,
        "See: \x1b]8;;https://github.com/user/repo\x07" "https://github.com/user/repo" "\x1b]8;;\x07" "\r\n"
        "for details.\r\n");

    /* Hyperlink should be on row 0, cols 5-32 */
    const char *url = wixen_terminal_hyperlink_at(&t, 5, 0);
    ASSERT(url != NULL);
    ASSERT_STR_EQ("https://github.com/user/repo", url);

    /* Text extraction should include the visible text, not the URL */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "See:") != NULL);
    ASSERT(strstr(r0, "https://github.com/user/repo") != NULL);
    free(r0);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Select command output, copy, strip VT === */

TEST red_e2e_select_and_strip(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);

    feeds(&t, &p,
        "$ ls\r\n"
        "\x1b[34mdir1\x1b[0m  \x1b[34mdir2\x1b[0m  file.txt\r\n"
        "$ ");

    /* Select the ls output (row 1) */
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 1, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 39, 1);
    char *selected = wixen_terminal_selected_text(&t, &sel);
    ASSERT(selected != NULL);
    /* Selected text should have content without escape codes */
    ASSERT(strstr(selected, "dir1") != NULL);
    ASSERT(strstr(selected, "file.txt") != NULL);
    ASSERT(strstr(selected, "\x1b") == NULL);
    free(selected);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === DSR response round-trip === */

TEST red_e2e_dsr_roundtrip(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);

    /* Move cursor to known position */
    feeds(&t, &p, "\x1b[5;10H");
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    ASSERT_EQ(9, (int)t.grid.cursor.col);

    /* Request cursor position */
    feeds(&t, &p, "\x1b[6n");

    /* Should get response ESC[5;10R */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "5;10R") != NULL);
    free((void *)resp);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === DECSC/DECRC with alt screen === */

TEST red_e2e_cursor_save_across_alt(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);

    /* Position cursor and save */
    feeds(&t, &p, "prompt> ");
    size_t saved_row = t.grid.cursor.row;
    size_t saved_col = t.grid.cursor.col;
    feeds(&t, &p, "\x1b" "7"); /* DECSC */

    /* Enter alt screen (this saves+clears) */
    feeds(&t, &p, "\x1b[?1049h");

    /* Do stuff on alt screen */
    feeds(&t, &p, "\x1b[5;5H" "alt content");

    /* Exit alt screen (this restores) */
    feeds(&t, &p, "\x1b[?1049l");

    /* Main screen text restored */
    char *r0 = wixen_terminal_extract_row_text(&t, 0);
    ASSERT(strstr(r0, "prompt>") != NULL);
    free(r0);

    /* Restore saved cursor */
    feeds(&t, &p, "\x1b" "8"); /* DECRC */
    ASSERT_EQ(saved_row, t.grid.cursor.row);
    ASSERT_EQ(saved_col, t.grid.cursor.col);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Resize during colored output === */

TEST red_e2e_resize_during_color(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);

    /* Colored output */
    feeds(&t, &p, "\x1b[1;31mError: \x1b[0mfile not found\r\n");
    feeds(&t, &p, "\x1b[32mSuccess: \x1b[0mcompleted\r\n");

    /* Resize smaller */
    wixen_terminal_resize(&t, 40, 12);

    /* Content should survive resize */
    char *text = wixen_terminal_visible_text(&t);
    ASSERT(text != NULL);
    /* At minimum some content should remain */
    ASSERT(strlen(text) > 0);
    free(text);

    /* No crash */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Shell integration with failed command === */

TEST red_e2e_shell_integ_error(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 10);
    wixen_parser_init(&p);

    feeds(&t, &p, "\x1b]133;A\x07" "$ ");
    feeds(&t, &p, "cat nonexistent.txt\r\n");
    feeds(&t, &p, "\x1b]133;C\x07");
    feeds(&t, &p, "cat: nonexistent.txt: No such file or directory\r\n");
    feeds(&t, &p, "\x1b]133;D;1\x07");

    /* Block should record exit code 1 */
    ASSERT(t.shell_integ.block_count >= 1);
    ASSERT(t.shell_integ.blocks[0].has_exit_code);
    ASSERT_EQ(1, t.shell_integ.blocks[0].exit_code);

    /* Format command completion announcement */
    char *announce = wixen_a11y_format_command_complete("cat nonexistent.txt", 1);
    ASSERT(announce != NULL);
    ASSERT(strstr(announce, "failed") != NULL);
    free(announce);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* === Prompt stripping from real terminal line === */

TEST red_e2e_prompt_strip_real(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 60, 5);
    wixen_parser_init(&p);

    feeds(&t, &p, "C:\\Users\\prati>echo hello");

    char *row = wixen_terminal_extract_row_text(&t, 0);
    char *stripped = wixen_strip_prompt(row);
    ASSERT_STR_EQ("echo hello", stripped);
    free(stripped);
    free(row);

    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_e2e_advanced) {
    RUN_TEST(red_e2e_hyperlink_in_output);
    RUN_TEST(red_e2e_select_and_strip);
    RUN_TEST(red_e2e_dsr_roundtrip);
    RUN_TEST(red_e2e_cursor_save_across_alt);
    RUN_TEST(red_e2e_resize_during_color);
    RUN_TEST(red_e2e_shell_integ_error);
    RUN_TEST(red_e2e_prompt_strip_real);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_e2e_advanced);
    GREATEST_MAIN_END();
}
