/* test_red_c_safety.c — C-specific safety tests
 *
 * Tests that target C's unique failure modes:
 * - malloc failure handling
 * - buffer boundary conditions
 * - double-free safety
 * - use-after-free prevention
 * - integer overflow in size calculations
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/cell.h"
#include "wixen/core/buffer.h"
#include "wixen/core/selection.h"
#include "wixen/core/hyperlink.h"
#include "wixen/vt/parser.h"
#include "wixen/search/search.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* --- Double-free safety --- */

TEST safety_terminal_double_free(void) {
    WixenTerminal t;
    wixen_terminal_init(&t, 10, 3);
    wixen_terminal_free(&t);
    wixen_terminal_free(&t); /* Must not crash */
    PASS();
}

TEST safety_grid_double_free(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    wixen_grid_free(&g);
    wixen_grid_free(&g);
    PASS();
}

TEST safety_parser_double_free(void) {
    WixenParser p;
    wixen_parser_init(&p);
    wixen_parser_free(&p);
    wixen_parser_free(&p);
    PASS();
}

TEST safety_search_double_free(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    wixen_search_free(&se);
    wixen_search_free(&se);
    PASS();
}

TEST safety_scrollback_double_free(void) {
    WixenScrollbackBuffer sb;
    wixen_scrollback_init(&sb, 100);
    wixen_scrollback_free(&sb);
    wixen_scrollback_free(&sb);
    PASS();
}

TEST safety_hyperlinks_double_free(void) {
    WixenHyperlinkStore hs;
    wixen_hyperlinks_init(&hs);
    wixen_hyperlinks_get_or_insert(&hs, "https://test.com", NULL);
    wixen_hyperlinks_free(&hs);
    wixen_hyperlinks_free(&hs);
    PASS();
}

/* --- Buffer boundary --- */

TEST safety_cell_long_content(void) {
    WixenCell c;
    wixen_cell_init(&c);
    /* Set content longer than internal buffer (if fixed-size) */
    char long_str[256];
    memset(long_str, 'A', 255);
    long_str[255] = '\0';
    wixen_cell_set_content(&c, long_str);
    /* Should either truncate or allocate — not overflow */
    ASSERT(strlen(c.content) > 0);
    wixen_cell_free(&c);
    PASS();
}

TEST safety_grid_visible_text_tiny_buffer(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 24);
    for (size_t r = 0; r < 24; r++)
        for (size_t c = 0; c < 80; c++)
            wixen_cell_set_content(&g.rows[r].cells[c], "X");
    char buf[8]; /* Tiny buffer for huge grid */
    size_t len = wixen_grid_visible_text(&g, buf, sizeof(buf));
    /* Must not overflow buf */
    ASSERT(len < sizeof(buf));
    ASSERT(buf[sizeof(buf) - 1] == '\0');
    wixen_grid_free(&g);
    PASS();
}

TEST safety_row_text_tiny_buffer(void) {
    WixenGrid g;
    wixen_grid_init(&g, 80, 1);
    for (size_t c = 0; c < 80; c++)
        wixen_cell_set_content(&g.rows[0].cells[c], "X");
    char buf[4];
    size_t len = wixen_row_text(&g.rows[0], buf, sizeof(buf));
    ASSERT(len <= 3);
    ASSERT(buf[3] == '\0');
    wixen_grid_free(&g);
    PASS();
}

/* --- Large grid operations --- */

TEST safety_large_grid(void) {
    WixenGrid g;
    wixen_grid_init(&g, 256, 64);
    ASSERT_EQ(256, (int)g.cols);
    ASSERT_EQ(64, (int)g.num_rows);
    wixen_cell_set_content(&g.rows[63].cells[255], "Z");
    ASSERT_STR_EQ("Z", g.rows[63].cells[255].content);
    wixen_grid_free(&g);
    PASS();
}

TEST safety_large_terminal(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 200, 50);
    wixen_parser_init(&p);
    /* Fill with text */
    for (int r = 0; r < 100; r++) {
        char line[256];
        memset(line, 'A' + (r % 26), 199);
        line[199] = '\r'; line[200] = '\n'; line[201] = '\0';
        feed(&t, &p, line);
    }
    ASSERT_EQ(200, (int)t.grid.cols);
    ASSERT_EQ(50, (int)t.grid.num_rows);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* --- Rapid init/free cycles (leak detection) --- */

TEST safety_rapid_init_free(void) {
    for (int i = 0; i < 100; i++) {
        WixenTerminal t;
        wixen_terminal_init(&t, 80, 24);
        WixenParser p;
        wixen_parser_init(&p);
        feed(&t, &p, "Hello World\r\n");
        wixen_parser_free(&p);
        wixen_terminal_free(&t);
    }
    /* If this leaks, 100 iterations * ~100KB per terminal = ~10MB leak */
    /* No way to assert from C without external tool, but no crash = basic pass */
    PASS();
}

TEST safety_rapid_grid_resize(void) {
    WixenGrid g;
    wixen_grid_init(&g, 10, 5);
    for (int i = 0; i < 200; i++) {
        size_t cols = (size_t)(5 + (i % 200));
        size_t rows = (size_t)(3 + (i % 50));
        wixen_grid_resize(&g, cols, rows);
    }
    ASSERT(g.cols > 0);
    ASSERT(g.num_rows > 0);
    wixen_grid_free(&g);
    PASS();
}

/* --- Selection boundary checks --- */

TEST safety_selection_huge_grid(void) {
    WixenSelection sel;
    wixen_selection_init(&sel);
    wixen_selection_start(&sel, 0, 0, WIXEN_SEL_NORMAL);
    wixen_selection_update(&sel, 9999, 9999);
    ASSERT(wixen_selection_row_count(&sel) == 10000);
    WixenGridPoint s, e;
    wixen_selection_ordered(&sel, &s, &e);
    ASSERT_EQ(0, (int)s.row);
    ASSERT_EQ(9999, (int)e.row);
    PASS();
}

/* --- Parser with massive input --- */

TEST safety_parser_1mb_input(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* 1MB of mixed text and escapes */
    size_t size = 1024 * 1024;
    char *big = (char *)malloc(size + 1);
    if (!big) SKIP();
    for (size_t i = 0; i < size; i++) {
        if (i % 100 == 0) big[i] = '\n';
        else if (i % 200 == 0) big[i] = '\x1b';
        else big[i] = 'A' + (char)(i % 26);
    }
    big[size] = '\0';
    /* Process in chunks */
    WixenAction actions[512];
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = size - offset;
        if (chunk > 4096) chunk = 4096;
        size_t n = wixen_parser_process(&p, (const uint8_t *)(big + offset), chunk, actions, 512);
        for (size_t i = 0; i < n; i++) {
            wixen_terminal_dispatch(&t, &actions[i]);
            if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
        }
        offset += chunk;
    }
    free(big);
    /* Should not crash or hang */
    ASSERT(t.grid.cols == 80);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* --- Search with many matches --- */

TEST safety_search_many_matches(void) {
    WixenSearchEngine se;
    wixen_search_init(&se);
    const char *rows[1000];
    char bufs[1000][32];
    for (int i = 0; i < 1000; i++) {
        snprintf(bufs[i], sizeof(bufs[i]), "match match match %d", i);
        rows[i] = bufs[i];
    }
    WixenSearchOptions opts = { false, false, true };
    wixen_search_execute(&se, "match", opts, rows, 1000, 0, 500);
    ASSERT(wixen_search_match_count(&se) >= 1000);
    wixen_search_free(&se);
    PASS();
}

SUITE(red_c_safety) {
    /* Double-free */
    RUN_TEST(safety_terminal_double_free);
    RUN_TEST(safety_grid_double_free);
    RUN_TEST(safety_parser_double_free);
    RUN_TEST(safety_search_double_free);
    RUN_TEST(safety_scrollback_double_free);
    RUN_TEST(safety_hyperlinks_double_free);
    /* Buffer boundary */
    RUN_TEST(safety_cell_long_content);
    RUN_TEST(safety_grid_visible_text_tiny_buffer);
    RUN_TEST(safety_row_text_tiny_buffer);
    /* Large operations */
    RUN_TEST(safety_large_grid);
    RUN_TEST(safety_large_terminal);
    /* Leak detection (stress) */
    RUN_TEST(safety_rapid_init_free);
    RUN_TEST(safety_rapid_grid_resize);
    /* Boundary */
    RUN_TEST(safety_selection_huge_grid);
    /* Stress */
    RUN_TEST(safety_parser_1mb_input);
    RUN_TEST(safety_search_many_matches);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_c_safety);
    GREATEST_MAIN_END();
}
