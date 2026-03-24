/* test_parser.c — Tests for VT500 parser state machine */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/vt/parser.h"

/* Helper: feed a string and collect all actions */
#define MAX_TEST_ACTIONS 256
static WixenAction actions[MAX_TEST_ACTIONS];
static size_t action_count;

static void feed(WixenParser *p, const char *s) {
    action_count = wixen_parser_process(p, (const uint8_t *)s, strlen(s),
                                         actions, MAX_TEST_ACTIONS);
}

static void feed_bytes(WixenParser *p, const uint8_t *data, size_t len) {
    action_count = wixen_parser_process(p, data, len, actions, MAX_TEST_ACTIONS);
}

/* === Ground State === */

TEST parser_printable_ascii(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "A");
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    ASSERT_EQ('A', (int)actions[0].codepoint);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_printable_sequence(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "Hello");
    ASSERT_EQ(5, (int)action_count);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(WIXEN_ACTION_PRINT, actions[i].type);
    }
    ASSERT_EQ('H', (int)actions[0].codepoint);
    ASSERT_EQ('o', (int)actions[4].codepoint);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_c0_control(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0x0A }; /* LF */
    feed_bytes(&p, data, 1);
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_EXECUTE, actions[0].type);
    ASSERT_EQ(0x0A, actions[0].control_byte);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_cr_lf(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0x0D, 0x0A }; /* CR LF */
    feed_bytes(&p, data, 2);
    ASSERT_EQ(2, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_EXECUTE, actions[0].type);
    ASSERT_EQ(0x0D, actions[0].control_byte);
    ASSERT_EQ(WIXEN_ACTION_EXECUTE, actions[1].type);
    ASSERT_EQ(0x0A, actions[1].control_byte);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_bell(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t bel = 0x07;
    feed_bytes(&p, &bel, 1);
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_EXECUTE, actions[0].type);
    ASSERT_EQ(0x07, actions[0].control_byte);
    wixen_parser_free(&p);
    PASS();
}

/* === CSI Sequences === */

TEST parser_csi_cursor_up(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[5A"); /* CSI 5 A — cursor up 5 */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('A', actions[0].csi.final_byte);
    ASSERT_EQ(1, actions[0].csi.param_count);
    ASSERT_EQ(5, actions[0].csi.params[0]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_cursor_position(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[10;20H"); /* CUP — row 10, col 20 */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('H', actions[0].csi.final_byte);
    ASSERT_EQ(2, actions[0].csi.param_count);
    ASSERT_EQ(10, actions[0].csi.params[0]);
    ASSERT_EQ(20, actions[0].csi.params[1]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_no_params(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[H"); /* CUP with no params — defaults */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('H', actions[0].csi.final_byte);
    ASSERT_EQ(0, actions[0].csi.param_count);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_sgr_bold(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[1m"); /* SGR bold */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('m', actions[0].csi.final_byte);
    ASSERT_EQ(1, actions[0].csi.param_count);
    ASSERT_EQ(1, actions[0].csi.params[0]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_sgr_256_color(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[38;5;196m"); /* FG 256-color index 196 */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('m', actions[0].csi.final_byte);
    ASSERT_EQ(3, actions[0].csi.param_count);
    ASSERT_EQ(38, actions[0].csi.params[0]);
    ASSERT_EQ(5, actions[0].csi.params[1]);
    ASSERT_EQ(196, actions[0].csi.params[2]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_sgr_rgb(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[38;2;255;128;0m"); /* FG RGB */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ(5, actions[0].csi.param_count);
    ASSERT_EQ(38, actions[0].csi.params[0]);
    ASSERT_EQ(2, actions[0].csi.params[1]);
    ASSERT_EQ(255, actions[0].csi.params[2]);
    ASSERT_EQ(128, actions[0].csi.params[3]);
    ASSERT_EQ(0, actions[0].csi.params[4]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_private_mode(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[?25h"); /* DECTCEM — show cursor */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('h', actions[0].csi.final_byte);
    ASSERT_EQ(1, actions[0].csi.param_count);
    ASSERT_EQ(25, actions[0].csi.params[0]);
    /* '?' should be in intermediates */
    ASSERT_EQ(1, actions[0].csi.intermediate_count);
    ASSERT_EQ('?', actions[0].csi.intermediates[0]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_erase_display(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[2J"); /* ED 2 — erase entire display */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ('J', actions[0].csi.final_byte);
    ASSERT_EQ(1, actions[0].csi.param_count);
    ASSERT_EQ(2, actions[0].csi.params[0]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_scroll_region(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b[5;20r"); /* DECSTBM — set scroll region rows 5-20 */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ('r', actions[0].csi.final_byte);
    ASSERT_EQ(2, actions[0].csi.param_count);
    ASSERT_EQ(5, actions[0].csi.params[0]);
    ASSERT_EQ(20, actions[0].csi.params[1]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_embedded_control(void) {
    WixenParser p;
    wixen_parser_init(&p);
    /* BEL embedded in CSI should be executed */
    uint8_t data[] = { 0x1b, '[', 0x07, '5', 'A' };
    feed_bytes(&p, data, sizeof(data));
    /* Should get: execute(BEL), then CSI 5 A */
    bool got_bel = false, got_csi = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_EXECUTE && actions[i].control_byte == 0x07)
            got_bel = true;
        if (actions[i].type == WIXEN_ACTION_CSI_DISPATCH && actions[i].csi.final_byte == 'A')
            got_csi = true;
    }
    ASSERT(got_bel);
    ASSERT(got_csi);
    wixen_parser_free(&p);
    PASS();
}

/* === ESC Sequences === */

TEST parser_esc_index(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b" "D"); /* IND — index (scroll up) */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_ESC_DISPATCH, actions[0].type);
    ASSERT_EQ('D', actions[0].esc.final_byte);
    ASSERT_EQ(0, actions[0].esc.intermediate_count);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_esc_reverse_index(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b" "M"); /* RI — reverse index (scroll down) */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_ESC_DISPATCH, actions[0].type);
    ASSERT_EQ('M', actions[0].esc.final_byte);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_esc_with_intermediate(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b#8"); /* DECALN — alignment test */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_ESC_DISPATCH, actions[0].type);
    ASSERT_EQ('8', actions[0].esc.final_byte);
    ASSERT_EQ(1, actions[0].esc.intermediate_count);
    ASSERT_EQ('#', actions[0].esc.intermediates[0]);
    wixen_parser_free(&p);
    PASS();
}

/* === OSC Sequences === */

TEST parser_osc_set_title(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b]0;Hello World\x07"); /* OSC 0 ; title BEL */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_OSC_DISPATCH, actions[0].type);
    ASSERT(actions[0].osc.data != NULL);
    /* Check payload contains "0;Hello World" */
    ASSERT(action_count > 0);
    char buf[256] = {0};
    size_t len = actions[0].osc.data_len < 255 ? actions[0].osc.data_len : 255;
    memcpy(buf, actions[0].osc.data, len);
    ASSERT_STR_EQ("0;Hello World", buf);
    free(actions[0].osc.data);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_osc_st_terminator(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b]2;Title\x1b\\"); /* OSC terminated by ESC \ (ST) */
    /* Should get OSC dispatch + ESC dispatch for '\' */
    bool got_osc = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            got_osc = true;
            free(actions[i].osc.data);
        }
    }
    ASSERT(got_osc);
    wixen_parser_free(&p);
    PASS();
}

/* === DCS Sequences === */

TEST parser_dcs_hook_and_unhook(void) {
    WixenParser p;
    wixen_parser_init(&p);
    /* DCS q (sixel) */
    uint8_t data[] = { 0x1b, 'P', 'q', '#', '0', 0x1b, '\\' };
    feed_bytes(&p, data, sizeof(data));
    bool got_hook = false, got_put = false, got_unhook = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_DCS_HOOK) got_hook = true;
        if (actions[i].type == WIXEN_ACTION_DCS_PUT) got_put = true;
        if (actions[i].type == WIXEN_ACTION_DCS_UNHOOK) got_unhook = true;
    }
    ASSERT(got_hook);
    ASSERT(got_put);
    ASSERT(got_unhook);
    wixen_parser_free(&p);
    PASS();
}

/* === APC === */

TEST parser_apc_kitty_graphics(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "\x1b_Gf=32;data\x1b\\"); /* APC G (Kitty) */
    bool got_apc = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_APC_DISPATCH) {
            got_apc = true;
            ASSERT(actions[i].apc.data_len > 0);
            free(actions[i].apc.data);
        }
    }
    ASSERT(got_apc);
    wixen_parser_free(&p);
    PASS();
}

/* === UTF-8 === */

TEST parser_utf8_2byte(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0xC3, 0xA9 }; /* é */
    feed_bytes(&p, data, 2);
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    ASSERT_EQ(0xE9, (int)actions[0].codepoint); /* U+00E9 */
    wixen_parser_free(&p);
    PASS();
}

TEST parser_utf8_3byte(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0xE4, 0xB8, 0xAD }; /* 中 U+4E2D */
    feed_bytes(&p, data, 3);
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    ASSERT_EQ(0x4E2D, (int)actions[0].codepoint);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_utf8_4byte(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0xF0, 0x9F, 0x98, 0x80 }; /* 😀 U+1F600 */
    feed_bytes(&p, data, 4);
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    ASSERT_EQ(0x1F600, (int)actions[0].codepoint);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_utf8_invalid_continuation(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0xC3, 0x41 }; /* Start of 2-byte, then ASCII */
    feed_bytes(&p, data, 2);
    /* Should get replacement char (from broken UTF-8) + 'A' might be consumed */
    bool got_replacement = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_PRINT && actions[i].codepoint == 0xFFFD)
            got_replacement = true;
    }
    ASSERT(got_replacement);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_utf8_overlong_rejected(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0xC0, 0x80 }; /* Overlong NUL */
    feed_bytes(&p, data, 2);
    /* Should emit replacement character */
    bool got_replacement = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_PRINT && actions[i].codepoint == 0xFFFD)
            got_replacement = true;
    }
    ASSERT(got_replacement);
    wixen_parser_free(&p);
    PASS();
}

/* === CAN/SUB abort === */

TEST parser_can_aborts_csi(void) {
    WixenParser p;
    wixen_parser_init(&p);
    uint8_t data[] = { 0x1b, '[', '5', 0x18, 'A' }; /* CSI 5 CAN A */
    feed_bytes(&p, data, sizeof(data));
    /* CAN aborts the CSI, then 'A' is printed in ground */
    bool got_csi = false, got_print = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_CSI_DISPATCH) got_csi = true;
        if (actions[i].type == WIXEN_ACTION_PRINT && actions[i].codepoint == 'A')
            got_print = true;
    }
    ASSERT_FALSE(got_csi); /* CSI was aborted */
    ASSERT(got_print);     /* A was printed */
    wixen_parser_free(&p);
    PASS();
}

/* === Mixed content === */

TEST parser_mixed_text_and_escapes(void) {
    WixenParser p;
    wixen_parser_init(&p);
    feed(&p, "AB\x1b[1mCD");
    /* Should get: Print(A), Print(B), CSI(1m), Print(C), Print(D) */
    ASSERT_EQ(5, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    ASSERT_EQ('A', (int)actions[0].codepoint);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[1].type);
    ASSERT_EQ('B', (int)actions[1].codepoint);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[2].type);
    ASSERT_EQ('m', actions[2].csi.final_byte);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[3].type);
    ASSERT_EQ('C', (int)actions[3].codepoint);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[4].type);
    ASSERT_EQ('D', (int)actions[4].codepoint);
    wixen_parser_free(&p);
    PASS();
}

/* === Edge Cases (C-specific) === */

TEST parser_empty_input(void) {
    WixenParser p;
    wixen_parser_init(&p);
    size_t count = wixen_parser_process(&p, NULL, 0, actions, MAX_TEST_ACTIONS);
    ASSERT_EQ(0, (int)count);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_reset_clears_state(void) {
    WixenParser p;
    wixen_parser_init(&p);
    /* Start a CSI sequence but don't finish it */
    feed(&p, "\x1b[5");
    ASSERT_EQ(WIXEN_PS_CSI_PARAM, p.state);
    wixen_parser_reset(&p);
    ASSERT_EQ(WIXEN_PS_GROUND, p.state);
    /* Now normal input should work */
    feed(&p, "A");
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_many_params(void) {
    WixenParser p;
    wixen_parser_init(&p);
    /* CSI with many semicolons */
    feed(&p, "\x1b[1;2;3;4;5;6;7;8;9;10m");
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ(10, actions[0].csi.param_count);
    ASSERT_EQ(1, actions[0].csi.params[0]);
    ASSERT_EQ(10, actions[0].csi.params[9]);
    wixen_parser_free(&p);
    PASS();
}

/* === Suites === */

SUITE(ground_tests) {
    RUN_TEST(parser_printable_ascii);
    RUN_TEST(parser_printable_sequence);
    RUN_TEST(parser_c0_control);
    RUN_TEST(parser_cr_lf);
    RUN_TEST(parser_bell);
}

SUITE(csi_tests) {
    RUN_TEST(parser_csi_cursor_up);
    RUN_TEST(parser_csi_cursor_position);
    RUN_TEST(parser_csi_no_params);
    RUN_TEST(parser_csi_sgr_bold);
    RUN_TEST(parser_csi_sgr_256_color);
    RUN_TEST(parser_csi_sgr_rgb);
    RUN_TEST(parser_csi_private_mode);
    RUN_TEST(parser_csi_erase_display);
    RUN_TEST(parser_csi_scroll_region);
    RUN_TEST(parser_csi_embedded_control);
}

SUITE(esc_tests) {
    RUN_TEST(parser_esc_index);
    RUN_TEST(parser_esc_reverse_index);
    RUN_TEST(parser_esc_with_intermediate);
}

SUITE(osc_tests) {
    RUN_TEST(parser_osc_set_title);
    RUN_TEST(parser_osc_st_terminator);
}

SUITE(dcs_tests) {
    RUN_TEST(parser_dcs_hook_and_unhook);
}

SUITE(apc_tests) {
    RUN_TEST(parser_apc_kitty_graphics);
}

SUITE(utf8_tests) {
    RUN_TEST(parser_utf8_2byte);
    RUN_TEST(parser_utf8_3byte);
    RUN_TEST(parser_utf8_4byte);
    RUN_TEST(parser_utf8_invalid_continuation);
    RUN_TEST(parser_utf8_overlong_rejected);
}

SUITE(parser_edge_cases) {
    RUN_TEST(parser_can_aborts_csi);
    RUN_TEST(parser_mixed_text_and_escapes);
    RUN_TEST(parser_empty_input);
    RUN_TEST(parser_reset_clears_state);
    RUN_TEST(parser_many_params);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(ground_tests);
    RUN_SUITE(csi_tests);
    RUN_SUITE(esc_tests);
    RUN_SUITE(osc_tests);
    RUN_SUITE(dcs_tests);
    RUN_SUITE(apc_tests);
    RUN_SUITE(utf8_tests);
    RUN_SUITE(parser_edge_cases);
    GREATEST_MAIN_END();
}
