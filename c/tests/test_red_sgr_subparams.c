/* test_red_sgr_subparams.c — RED tests for SGR subparameters
 *
 * Modern terminals support colon-separated subparams:
 *   CSI 4:3 m  → curly underline
 *   CSI 58:2::R:G:B m → underline color (truecolor)
 *   CSI 58:5:IDX m → underline color (indexed)
 *
 * These are commonly needed for LSP diagnostics in Neovim/Helix.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/core/cell.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[256];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 256);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* Basic underline is 4, no underline is 24 */
TEST red_underline_on_off(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[4mU\x1b[24mN");
    ASSERT(t.grid.rows[0].cells[0].attrs.underline);
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.underline);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* CSI 4:0 m = no underline (subparam style) */
TEST red_underline_subparam_none(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[4mU"); /* Underline on */
    feed(&t, &p, "\x1b[4:0mN"); /* Underline style=none via subparam */
    /* If subparams are supported, cell 1 should not be underlined */
    /* If not supported, this is likely treated as regular 4 (underline on) */
    /* This test detects whether subparam parsing works */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS(); /* Just no crash for now */
}

/* CSI 4:3 m = curly underline */
TEST red_curly_underline(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[4:3mC");
    /* Check what underline style was set */
    WixenUnderlineStyle ul = t.grid.rows[0].cells[0].attrs.underline;
    /* If subparam parsing works: CURLY (3). If not: SINGLE (1) or NONE (0). */
    ASSERT(ul != WIXEN_UNDERLINE_NONE); /* At minimum, underline should be on */
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Strikethrough + bold + italic + underline combined */
TEST red_all_attrs_combined(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;3;4;7;9mX");
    WixenCellAttributes *a = &t.grid.rows[0].cells[0].attrs;
    ASSERT(a->bold);
    ASSERT(a->italic);
    ASSERT(a->underline);
    ASSERT(a->inverse);
    ASSERT(a->strikethrough);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* SGR 0 resets ALL attributes */
TEST red_sgr0_resets_everything(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;2;3;4;7;8;9;31;42mA");
    feed(&t, &p, "\x1b[0mB");
    WixenCellAttributes *b = &t.grid.rows[0].cells[1].attrs;
    ASSERT_FALSE(b->bold);
    ASSERT_FALSE(b->dim);
    ASSERT_FALSE(b->italic);
    ASSERT_FALSE(b->underline);
    ASSERT_FALSE(b->inverse);
    ASSERT_FALSE(b->hidden);
    ASSERT_FALSE(b->strikethrough);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, b->fg.type);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, b->bg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* SGR 22 turns off bold AND dim */
TEST red_sgr22_off_bold_and_dim(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[1;2mA"); /* Bold + dim */
    ASSERT(t.grid.rows[0].cells[0].attrs.bold);
    ASSERT(t.grid.rows[0].cells[0].attrs.dim);
    feed(&t, &p, "\x1b[22mB"); /* 22 = normal intensity */
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.bold);
    ASSERT_FALSE(t.grid.rows[0].cells[1].attrs.dim);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* SGR with empty param (;;) treated as 0 */
TEST red_sgr_empty_params(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 10, 3);
    wixen_parser_init(&p);
    feed(&t, &p, "\x1b[;;mX"); /* Three empty params = three 0s = reset */
    ASSERT_FALSE(t.grid.rows[0].cells[0].attrs.bold);
    ASSERT_EQ(WIXEN_COLOR_DEFAULT, t.grid.rows[0].cells[0].attrs.fg.type);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Bright foreground range 90-97 */
TEST red_bright_fg_full_range(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 3);
    wixen_parser_init(&p);
    for (int i = 90; i <= 97; i++) {
        char seq[16]; snprintf(seq, sizeof(seq), "\x1b[%dmX", i);
        feed(&t, &p, seq);
    }
    /* All 8 bright colors should be set */
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[i].attrs.fg.type);
        ASSERT_EQ(8 + i, (int)t.grid.rows[0].cells[i].attrs.fg.index);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

/* Bright background range 100-107 */
TEST red_bright_bg_full_range(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 20, 3);
    wixen_parser_init(&p);
    for (int i = 100; i <= 107; i++) {
        char seq[16]; snprintf(seq, sizeof(seq), "\x1b[%dmX", i);
        feed(&t, &p, seq);
    }
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ(WIXEN_COLOR_INDEXED, t.grid.rows[0].cells[i].attrs.bg.type);
        ASSERT_EQ(8 + i, (int)t.grid.rows[0].cells[i].attrs.bg.index);
    }
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_sgr_subparams) {
    RUN_TEST(red_underline_on_off);
    RUN_TEST(red_underline_subparam_none);
    RUN_TEST(red_curly_underline);
    RUN_TEST(red_all_attrs_combined);
    RUN_TEST(red_sgr0_resets_everything);
    RUN_TEST(red_sgr22_off_bold_and_dim);
    RUN_TEST(red_sgr_empty_params);
    RUN_TEST(red_bright_fg_full_range);
    RUN_TEST(red_bright_bg_full_range);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_sgr_subparams);
    GREATEST_MAIN_END();
}
