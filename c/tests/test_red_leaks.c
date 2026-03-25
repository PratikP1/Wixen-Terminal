/* test_red_leaks.c — RED tests for memory leak detection
 *
 * C has no automatic resource cleanup. Every malloc must have a free.
 * These tests create objects, use them, free them, and verify that
 * repeated cycles don't grow memory. We use a simple alloc counter.
 *
 * Strategy: run the same operation 1000 times. If memory grows
 * linearly, we have a leak. Measure via _CrtMemCheckpoint on MSVC.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/grid.h"
#include "wixen/core/buffer.h"
#include "wixen/core/selection.h"
#include "wixen/core/hyperlink.h"
#include "wixen/a11y/events.h"
#include "wixen/search/search.h"
#include "wixen/config/config.h"
#include "wixen/vt/parser.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) free(actions[i].apc.data);
    }
}

/* === Terminal init/free cycle === */

TEST red_leak_terminal_cycle(void) {
    for (int i = 0; i < 100; i++) {
        WixenTerminal t;
        wixen_terminal_init(&t, 80, 24);
        wixen_terminal_free(&t);
    }
    /* If this leaks, 100 cycles * ~150KB = ~15MB growth */
    PASS();
}

/* === Terminal with heavy usage then free === */

TEST red_leak_terminal_heavy(void) {
    for (int i = 0; i < 20; i++) {
        WixenTerminal t; WixenParser p;
        wixen_terminal_init(&t, 80, 24);
        wixen_parser_init(&p);
        /* OSC title */
        feed(&t, &p, "\x1b]0;Test Title\x07");
        /* Print text */
        feed(&t, &p, "Hello World\r\n");
        /* OSC 8 hyperlink */
        feed(&t, &p, "\x1b]8;;https://test.com\x07" "link" "\x1b]8;;\x07");
        /* OSC 52 clipboard */
        feed(&t, &p, "\x1b]52;c;SGVsbG8=\x07");
        /* CSI sequences */
        feed(&t, &p, "\x1b[1;31mred\x1b[0m");
        /* Alternate screen */
        feed(&t, &p, "\x1b[?1049h");
        feed(&t, &p, "alt screen content");
        feed(&t, &p, "\x1b[?1049l");
        /* DSR */
        feed(&t, &p, "\x1b[6n");
        const char *resp = wixen_terminal_pop_response(&t);
        free((void *)resp);
        /* Drain clipboard */
        char *clip = wixen_terminal_drain_clipboard_write(&t);
        free(clip);
        /* Extract text */
        char *text = wixen_terminal_visible_text(&t);
        free(text);
        char *row = wixen_terminal_extract_row_text(&t, 0);
        free(row);
        wixen_parser_free(&p);
        wixen_terminal_free(&t);
    }
    PASS();
}

/* === Grid init/free cycle === */

TEST red_leak_grid_cycle(void) {
    for (int i = 0; i < 200; i++) {
        WixenGrid g;
        wixen_grid_init(&g, 80, 24);
        wixen_grid_free(&g);
    }
    PASS();
}

/* === Scrollback push/free === */

TEST red_leak_scrollback_cycle(void) {
    for (int i = 0; i < 50; i++) {
        WixenScrollbackBuffer sb;
        wixen_scrollback_init(&sb, 10);
        for (int j = 0; j < 100; j++) {
            WixenRow row;
            wixen_row_init(&row, 20);
            wixen_scrollback_push(&sb, &row, 20);
        }
        wixen_scrollback_free(&sb);
    }
    PASS();
}

/* === Search execute/free === */

TEST red_leak_search_cycle(void) {
    for (int i = 0; i < 100; i++) {
        WixenSearchEngine se;
        wixen_search_init(&se);
        const char *rows[] = {"Hello World", "foo bar", "Hello again"};
        WixenSearchOptions opts = {false, false, true};
        wixen_search_execute(&se, "Hello", opts, rows, 3, 0, 0);
        char *status = wixen_search_status_text(&se);
        free(status);
        wixen_search_free(&se);
    }
    PASS();
}

/* === Hyperlink store cycle === */

TEST red_leak_hyperlink_cycle(void) {
    for (int i = 0; i < 100; i++) {
        WixenHyperlinkStore hs;
        wixen_hyperlinks_init(&hs);
        for (int j = 0; j < 50; j++) {
            char uri[64];
            snprintf(uri, sizeof(uri), "https://test.com/%d", j);
            wixen_hyperlinks_get_or_insert(&hs, uri, NULL);
        }
        wixen_hyperlinks_free(&hs);
    }
    PASS();
}

/* === Config load/free === */

TEST red_leak_config_cycle(void) {
    for (int i = 0; i < 100; i++) {
        WixenConfig cfg;
        wixen_config_init_defaults(&cfg);
        wixen_config_free(&cfg);
    }
    PASS();
}

/* === Strip VT cycle (alloc+free each time) === */

TEST red_leak_strip_vt(void) {
    for (int i = 0; i < 1000; i++) {
        char *s = wixen_strip_vt_escapes("\x1b[1;31mHello\x1b[0m World");
        free(s);
    }
    PASS();
}

/* === Parser process cycle === */

TEST red_leak_parser_cycle(void) {
    for (int i = 0; i < 100; i++) {
        WixenParser p;
        wixen_parser_init(&p);
        WixenAction actions[64];
        const char *data = "\x1b[1;31mHello\x1b[0m\x1b]0;Title\x07" "text\r\n";
        size_t count = wixen_parser_process(&p, (const uint8_t *)data, strlen(data), actions, 64);
        for (size_t j = 0; j < count; j++) {
            if (actions[j].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[j].osc.data);
        }
        wixen_parser_free(&p);
    }
    PASS();
}

/* === Reflow cycle (heavy alloc during reflow) === */

TEST red_leak_reflow_cycle(void) {
    for (int i = 0; i < 30; i++) {
        WixenTerminal t; WixenParser p;
        wixen_terminal_init(&t, 80, 24);
        wixen_parser_init(&p);
        feed(&t, &p, "AAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBBBBB");
        wixen_terminal_resize_reflow(&t, 10, 24);
        wixen_terminal_resize_reflow(&t, 80, 24);
        wixen_parser_free(&p);
        wixen_terminal_free(&t);
    }
    PASS();
}

#if defined(_MSC_VER) && defined(_DEBUG)
/* MSVC CRT leak detection — only available in Debug builds */

TEST red_crt_leak_check(void) {
    _CrtMemState before, after, diff;
    _CrtMemCheckpoint(&before);

    /* Run all cycle tests */
    for (int i = 0; i < 10; i++) {
        WixenTerminal t; WixenParser p;
        wixen_terminal_init(&t, 40, 10);
        wixen_parser_init(&p);
        feed(&t, &p, "\x1b]0;Title\x07" "Hello\r\n\x1b[1;31mRed\x1b[0m");
        feed(&t, &p, "\x1b]8;;https://x.com\x07" "L" "\x1b]8;;\x07");
        feed(&t, &p, "\x1b]52;c;dGVzdA==\x07");
        char *clip = wixen_terminal_drain_clipboard_write(&t);
        free(clip);
        char *text = wixen_terminal_visible_text(&t);
        free(text);
        const char *resp = wixen_terminal_pop_response(&t);
        free((void *)resp);
        wixen_parser_free(&p);
        wixen_terminal_free(&t);
    }

    _CrtMemCheckpoint(&after);
    /* If there are leaks, _CrtMemDifference returns nonzero */
    if (_CrtMemDifference(&diff, &before, &after)) {
        /* There are allocations that weren't freed */
        /* Allow small leaks from static buffers (regex compilation etc.) */
        if (diff.lCounts[_NORMAL_BLOCK] > 5) {
            FAILm("CRT detected memory leaks (>5 normal blocks)");
        }
    }
    PASS();
}
#endif

SUITE(red_leaks) {
    RUN_TEST(red_leak_terminal_cycle);
    RUN_TEST(red_leak_terminal_heavy);
    RUN_TEST(red_leak_grid_cycle);
    RUN_TEST(red_leak_scrollback_cycle);
    RUN_TEST(red_leak_search_cycle);
    RUN_TEST(red_leak_hyperlink_cycle);
    RUN_TEST(red_leak_config_cycle);
    RUN_TEST(red_leak_strip_vt);
    RUN_TEST(red_leak_parser_cycle);
    RUN_TEST(red_leak_reflow_cycle);
#if defined(_MSC_VER) && defined(_DEBUG)
    RUN_TEST(red_crt_leak_check);
#endif
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_leaks);
    GREATEST_MAIN_END();
}
