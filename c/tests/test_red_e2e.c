/* test_red_e2e.c — End-to-end tests with real terminal output sequences
 *
 * These feed actual VT sequences that real programs produce and verify
 * the terminal state matches expected output.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[1024];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 1024);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

static const char *cell(WixenTerminal *t, size_t row, size_t col) {
    return t->grid.rows[row].cells[col].content;
}

static char *row_text(WixenTerminal *t, size_t row) {
    static char buf[256];
    wixen_row_text(&t->grid.rows[row], buf, sizeof(buf));
    return buf;
}

/* Simulate: cmd.exe prompt + dir command header */
TEST e2e_cmd_dir(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feed(&t, &p,
        "C:\\Users\\test>dir\r\n"
        " Volume in drive C has no label.\r\n"
        " Volume Serial Number is ABCD-1234\r\n"
        "\r\n"
        " Directory of C:\\Users\\test\r\n"
        "\r\n"
        "03/23/2026  09:00 AM    <DIR>          .\r\n"
        "03/23/2026  09:00 AM    <DIR>          ..\r\n"
        "03/23/2026  10:15 AM             1,234 file.txt\r\n"
    );
    ASSERT_STR_EQ("C", cell(&t, 0, 0));
    ASSERT(strstr(row_text(&t, 0), "dir") != NULL);
    ASSERT(strstr(row_text(&t, 1), "Volume") != NULL);
    ASSERT(strstr(row_text(&t, 6), "DIR") != NULL);
    ASSERT(strstr(row_text(&t, 8), "file.txt") != NULL);
    ASSERT_EQ(9, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: colored git status output */
TEST e2e_git_status(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feed(&t, &p,
        "On branch \x1b[32mmain\x1b[0m\r\n"
        "Changes not staged for commit:\r\n"
        "  (use \"git add <file>...\" to update)\r\n"
        "\r\n"
        "\t\x1b[31mmodified:   src/main.c\x1b[0m\r\n"
        "\r\n"
        "no changes added to commit\r\n"
    );
    /* "main" should be green */
    ASSERT(strstr(row_text(&t, 0), "main") != NULL);
    /* Find "main" position — after "On branch " (10 chars) */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[10].attrs.fg.type);
    ASSERT_EQ(2, (int)t.grid.rows[0].cells[10].attrs.fg.index); /* Green */
    /* "modified:" should be red */
    /* Tab moves to col 8, then red text starts */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[4].cells[8].attrs.fg.type);
    ASSERT_EQ(1, (int)t.grid.rows[4].cells[8].attrs.fg.index); /* Red */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: cargo build with error */
TEST e2e_cargo_error(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    feed(&t, &p,
        "   \x1b[1m\x1b[32mCompiling\x1b[0m wixen v0.1.0\r\n"
        "\x1b[1m\x1b[38;5;9merror\x1b[0m\x1b[1m: mismatched types\x1b[0m\r\n"
        "  \x1b[1m\x1b[38;5;12m-->\x1b[0m src/main.rs:42:10\r\n"
        "   \x1b[1m\x1b[38;5;12m|\x1b[0m\r\n"
        "\x1b[1m\x1b[38;5;12m42\x1b[0m \x1b[1m\x1b[38;5;12m|\x1b[0m     let x: u32 = \"hello\";\r\n"
    );
    /* "Compiling" should be bold green */
    ASSERT(t.grid.rows[0].cells[3].attrs.bold);
    ASSERT_EQ(2, (int)t.grid.rows[0].cells[3].attrs.fg.index);
    /* "error" on row 1 should be bright red (index 9 via 256-color) */
    ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[1].cells[0].attrs.fg.type);
    ASSERT_EQ(9, (int)t.grid.rows[1].cells[0].attrs.fg.index);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: clear screen + redraw (like top/htop) */
TEST e2e_clear_redraw(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Old content line 1\r\nOld line 2\r\n");
    /* Clear and redraw */
    feed(&t, &p, "\x1b[2J\x1b[H");  /* Clear + home */
    feed(&t, &p,
        "CPU:  5.2%\r\n"
        "Mem: 4.1G/16G\r\n"
        "Tasks: 142\r\n"
    );
    ASSERT_STR_EQ("C", cell(&t, 0, 0));
    ASSERT(strstr(row_text(&t, 0), "CPU") != NULL);
    ASSERT(strstr(row_text(&t, 1), "Mem") != NULL);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: vim-like alternate screen */
TEST e2e_vim_alt_screen(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    /* Normal shell prompt */
    feed(&t, &p, "$ vim file.txt\r\n");
    ASSERT_STR_EQ("$", cell(&t, 0, 0));
    /* Enter alt screen + app cursor + hide cursor */
    feed(&t, &p, "\x1b[?1049h\x1b[?1h\x1b[?25l");
    ASSERT(t.modes.alternate_screen);
    ASSERT(t.modes.cursor_keys_application);
    ASSERT_FALSE(t.modes.cursor_visible);
    /* Draw status line */
    feed(&t, &p, "\x1b[10;1H\x1b[7m-- INSERT --\x1b[0m");
    ASSERT(t.grid.rows[9].cells[0].attrs.inverse);
    /* Exit alt screen */
    feed(&t, &p, "\x1b[?1049l\x1b[?1l\x1b[?25h");
    ASSERT_FALSE(t.modes.alternate_screen);
    ASSERT_FALSE(t.modes.cursor_keys_application);
    ASSERT(t.modes.cursor_visible);
    /* Shell prompt should be restored */
    ASSERT_STR_EQ("$", cell(&t, 0, 0));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: PowerShell prompt with OSC 133 */
TEST e2e_pwsh_osc133(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* OSC 133 A = prompt start */
    feed(&t, &p, "\x1b]133;A\x07");
    feed(&t, &p, "PS C:\\Users\\test> ");
    /* OSC 133 B = command input */
    feed(&t, &p, "\x1b]133;B\x07");
    feed(&t, &p, "Get-Process");
    feed(&t, &p, "\r\n");
    /* OSC 133 C = command output start */
    feed(&t, &p, "\x1b]133;C\x07");
    feed(&t, &p, "Handles  NPM(K)  PM(K)  CPU(s)    Name\r\n");
    feed(&t, &p, "-------  ------  -----  ------    ----\r\n");
    feed(&t, &p, "    123       5   1234    0.50    explorer\r\n");
    /* OSC 133 D = command done, exit code 0 */
    feed(&t, &p, "\x1b]133;D;0\x07");
    /* Terminal should have rendered all text correctly */
    ASSERT(strstr(row_text(&t, 0), "PS C:") != NULL);
    /* "Handles" header is on row 1 (after prompt+command+\r\n) */
    bool found_handles = false;
    for (int r = 0; r < 10; r++) {
        if (strstr(row_text(&t, (size_t)r), "Handles") != NULL) {
            found_handles = true; break;
        }
    }
    ASSERT(found_handles);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: long output that scrolls */
TEST e2e_scroll_output(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    for (int i = 0; i < 20; i++) {
        char line[64];
        snprintf(line, sizeof(line), "Output line %02d\r\n", i);
        feed(&t, &p, line);
    }
    /* Only last 5 lines visible (rows 0-4 show lines 16-20) */
    /* Line 15 would have been at the scroll edge */
    ASSERT(strstr(row_text(&t, 0), "Output line") != NULL);
    /* Cursor at bottom */
    ASSERT_EQ(4, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: progress bar with carriage return overwrites */
TEST e2e_progress_bar(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Downloading... [====      ] 40%\r");
    feed(&t, &p, "Downloading... [========  ] 80%\r");
    feed(&t, &p, "Downloading... [==========] 100%\r\n");
    /* Last overwrite should show 100% */
    ASSERT(strstr(row_text(&t, 0), "100%") != NULL);
    /* Cursor moved to line 1 after \r\n */
    ASSERT_EQ(1, (int)t.grid.cursor.row);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Simulate: tab-separated columns */
TEST e2e_tab_columns(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 5);
    wixen_parser_init(&p);
    feed(&t, &p, "Name\tSize\tDate\r\n");
    feed(&t, &p, "file.txt\t1234\t2026-03-24\r\n");
    /* "Name" at col 0, "Size" at col 8, "Date" at col 16 */
    ASSERT_STR_EQ("N", cell(&t, 0, 0));
    ASSERT_STR_EQ("S", cell(&t, 0, 8));
    ASSERT_STR_EQ("D", cell(&t, 0, 16));
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_e2e) {
    RUN_TEST(e2e_cmd_dir);
    RUN_TEST(e2e_git_status);
    RUN_TEST(e2e_cargo_error);
    RUN_TEST(e2e_clear_redraw);
    RUN_TEST(e2e_vim_alt_screen);
    RUN_TEST(e2e_pwsh_osc133);
    RUN_TEST(e2e_scroll_output);
    RUN_TEST(e2e_progress_bar);
    RUN_TEST(e2e_tab_columns);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_e2e);
    GREATEST_MAIN_END();
}
