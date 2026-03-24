/* test_stress.c — Stress tests and edge cases across all modules */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/vt/parser.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/buffer.h"
#include "wixen/core/selection.h"
#include "wixen/core/cell.h"

/* === Parser stress === */

TEST stress_parser_rapid_csi(void) {
    WixenParser p;
    wixen_parser_init(&p);
    WixenAction *actions = (WixenAction *)malloc(1024 * sizeof(WixenAction));
    ASSERT(actions != NULL);
    char *buf = (char *)malloc(16384);
    ASSERT(buf != NULL);
    size_t pos = 0;
    for (int i = 0; i < 500 && pos < 15000; i++) {
        pos += (size_t)sprintf(buf + pos, "\x1b[%dA", i % 100);
    }
    size_t count = wixen_parser_process(&p, (uint8_t *)buf, pos, actions, 1024);
    ASSERT(count >= 400);
    free(buf);
    free(actions);
    wixen_parser_free(&p);
    PASS();
}

TEST stress_parser_long_osc(void) {
    WixenParser p;
    wixen_parser_init(&p);
    /* Very long OSC title */
    char *buf = (char *)malloc(8192);
    ASSERT(buf != NULL);
    size_t pos = 0;
    buf[pos++] = '\x1b'; buf[pos++] = ']'; buf[pos++] = '0'; buf[pos++] = ';';
    for (int i = 0; i < 4000; i++) buf[pos++] = 'A';
    buf[pos++] = '\x07';
    WixenAction actions[16];
    size_t count = wixen_parser_process(&p, (uint8_t *)buf, pos, actions, 16);
    /* Should get one OSC dispatch */
    bool got_osc = false;
    for (size_t i = 0; i < count; i++) {
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            got_osc = true;
            free(actions[i].osc.data);
        }
    }
    ASSERT(got_osc);
    free(buf);
    wixen_parser_free(&p);
    PASS();
}

/* === Terminal stress === */

TEST stress_terminal_rapid_output(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    WixenParser p;
    wixen_parser_init(&p);
    /* Simulate 100 lines of output */
    WixenAction act_buf[64];
    for (int i = 0; i < 100; i++) {
        char line[40];
        int len = sprintf(line, "Line %03d\r\n", i);
        size_t count = wixen_parser_process(&p, (uint8_t *)line, (size_t)len, act_buf, 64);
        for (size_t j = 0; j < count; j++) {
            wixen_terminal_dispatch(&t, &act_buf[j]);
        }
    }
    /* Should not crash, cursor at bottom */
    ASSERT_EQ(23, (int)t.grid.cursor.row);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST stress_terminal_rapid_cursor(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 80, 24);
    WixenParser p;
    wixen_parser_init(&p);
    /* 1000 rapid cursor movements */
    WixenAction cact[8];
    for (int i = 0; i < 1000; i++) {
        char cmd[16];
        int len = sprintf(cmd, "\x1b[%d;%dH", (i % 24) + 1, (i % 80) + 1);
        size_t count = wixen_parser_process(&p, (uint8_t *)cmd, (size_t)len, cact, 8);
        for (size_t j = 0; j < count; j++)
            wixen_terminal_dispatch(&t, &cact[j]);
    }
    /* Should not crash */
    ASSERT(t.grid.cursor.row < 24);
    ASSERT(t.grid.cursor.col < 80);
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

/* === Buffer stress === */

TEST stress_buffer_large(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 20); /* Low threshold to trigger compression */
    for (int i = 0; i < 50; i++) {
        WixenRow row;
        wixen_row_init(&row, 80);
        char content[8];
        sprintf(content, "%d", i);
        wixen_cell_set_content(&row.cells[0], content);
        wixen_scrollback_push(&sb, &row, 80);
    }
    ASSERT_EQ(50, (int)wixen_scrollback_len(&sb));
    ASSERT(wixen_scrollback_cold_blocks(&sb) > 0);
    /* Hot rows should still be accessible */
    ASSERT(wixen_scrollback_hot_len(&sb) > 0);
    wixen_scrollback_free(&sb);
    PASS();
}

/* === Grid stress === */

TEST stress_grid_many_scrolls(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    for (int i = 0; i < 1000; i++) {
        wixen_grid_scroll_up(&g, 1);
    }
    /* Should not crash */
    ASSERT_EQ(24, (int)g.num_rows);
    wixen_grid_free(&g);
    PASS();
}

TEST stress_grid_insert_delete_cycle(void) {
    WixenGrid g;
    wixen_grid_init(&g, 20, 10);
    for (int i = 0; i < 100; i++) {
        wixen_grid_insert_blank_cells(&g, 5, 3, 3);
        wixen_grid_delete_cells(&g, 5, 3, 3);
    }
    ASSERT_EQ(20, (int)g.cols);
    wixen_grid_free(&g);
    PASS();
}

/* === Selection edge cases === */

TEST stress_selection_zero_size_block(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 5, 5, WIXEN_SEL_BLOCK);
    /* No update — anchor == end, zero-area block */
    ASSERT(wixen_selection_contains(&sel, 5, 5, 80));
    ASSERT_FALSE(wixen_selection_contains(&sel, 4, 5, 80));
    PASS();
}

/* === Cell memory === */

TEST stress_cell_rapid_content_change(void) {
    WixenCell cell;
    wixen_cell_init(&cell);
    for (int i = 0; i < 1000; i++) {
        char buf[16];
        sprintf(buf, "%d", i);
        wixen_cell_set_content(&cell, buf);
    }
    ASSERT_STR_EQ("999", cell.content);
    wixen_cell_free(&cell);
    PASS();
}

SUITE(parser_stress) {
    RUN_TEST(stress_parser_rapid_csi);
    RUN_TEST(stress_parser_long_osc);
}

SUITE(terminal_stress) {
    RUN_TEST(stress_terminal_rapid_output);
    RUN_TEST(stress_terminal_rapid_cursor);
}

SUITE(buffer_stress) {
    RUN_TEST(stress_buffer_large);
}

SUITE(grid_stress) {
    RUN_TEST(stress_grid_many_scrolls);
    RUN_TEST(stress_grid_insert_delete_cycle);
}

SUITE(misc_stress) {
    RUN_TEST(stress_selection_zero_size_block);
    RUN_TEST(stress_cell_rapid_content_change);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(parser_stress);
    RUN_SUITE(terminal_stress);
    RUN_SUITE(buffer_stress);
    RUN_SUITE(grid_stress);
    RUN_SUITE(misc_stress);
    GREATEST_MAIN_END();
}
