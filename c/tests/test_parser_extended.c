/* test_parser_extended.c — Additional parser edge case tests */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/vt/parser.h"

static WixenAction actions[256];
static size_t action_count;

static void feed(WixenParser *p, const char *s) {
    action_count = wixen_parser_process(p, (const uint8_t *)s, strlen(s), actions, 256);
}

TEST parser_8bit_csi(void) {
    WixenParser p; wixen_parser_init(&p);
    uint8_t data[] = { 0x9B, '5', 'A' }; /* 8-bit CSI */
    action_count = wixen_parser_process(&p, data, 3, actions, 256);
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[0].type);
    ASSERT_EQ('A', actions[0].csi.final_byte);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_sub_cancels(void) {
    WixenParser p; wixen_parser_init(&p);
    uint8_t data[] = { 0x1b, '[', '5', 0x1A, 'B' }; /* CSI 5 SUB B */
    action_count = wixen_parser_process(&p, data, 5, actions, 256);
    bool got_csi = false, got_print = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_CSI_DISPATCH) got_csi = true;
        if (actions[i].type == WIXEN_ACTION_PRINT && actions[i].codepoint == 'B') got_print = true;
    }
    ASSERT_FALSE(got_csi);
    ASSERT(got_print);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_esc_with_two_intermediates(void) {
    WixenParser p; wixen_parser_init(&p);
    feed(&p, "\x1b(B"); /* ESC ( B — designate G0 charset */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_ESC_DISPATCH, actions[0].type);
    ASSERT_EQ('B', actions[0].esc.final_byte);
    ASSERT_EQ(1, actions[0].esc.intermediate_count);
    ASSERT_EQ('(', actions[0].esc.intermediates[0]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_csi_semicolons_empty_params(void) {
    WixenParser p; wixen_parser_init(&p);
    feed(&p, "\x1b[;;H"); /* Two empty params + H */
    ASSERT_EQ(1, (int)action_count);
    ASSERT_EQ('H', actions[0].csi.final_byte);
    /* Empty params should be 0 */
    ASSERT_EQ(0, actions[0].csi.params[0]);
    ASSERT_EQ(0, actions[0].csi.params[1]);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_esc_esc_restarts(void) {
    WixenParser p; wixen_parser_init(&p);
    feed(&p, "\x1b\x1b[5A"); /* ESC ESC [ 5 A */
    /* First ESC starts escape, second ESC restarts, then CSI 5A */
    bool got_csi = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_CSI_DISPATCH && actions[i].csi.final_byte == 'A')
            got_csi = true;
    }
    ASSERT(got_csi);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_osc_empty_payload(void) {
    WixenParser p; wixen_parser_init(&p);
    feed(&p, "\x1b]\x07"); /* OSC with no content then BEL */
    bool got_osc = false;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            got_osc = true;
            ASSERT_EQ(0, (int)actions[i].osc.data_len);
            free(actions[i].osc.data);
        }
    }
    ASSERT(got_osc);
    wixen_parser_free(&p);
    PASS();
}

TEST parser_ground_control_codes(void) {
    WixenParser p; wixen_parser_init(&p);
    uint8_t data[] = { 0x00, 0x01, 0x02, 0x03 }; /* NUL, SOH, STX, ETX */
    action_count = wixen_parser_process(&p, data, 4, actions, 256);
    ASSERT_EQ(4, (int)action_count);
    for (size_t i = 0; i < 4; i++) {
        ASSERT_EQ(WIXEN_ACTION_EXECUTE, actions[i].type);
    }
    wixen_parser_free(&p);
    PASS();
}

TEST parser_mixed_utf8_and_csi(void) {
    WixenParser p; wixen_parser_init(&p);
    /* é (2-byte UTF-8) + CSI bold + A */
    uint8_t data[] = { 0xC3, 0xA9, 0x1B, '[', '1', 'm', 'A' };
    action_count = wixen_parser_process(&p, data, 7, actions, 256);
    /* Should get: Print(é), CSI(1m), Print(A) */
    ASSERT_EQ(3, (int)action_count);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[0].type);
    ASSERT_EQ(0xE9, (int)actions[0].codepoint);
    ASSERT_EQ(WIXEN_ACTION_CSI_DISPATCH, actions[1].type);
    ASSERT_EQ(WIXEN_ACTION_PRINT, actions[2].type);
    ASSERT_EQ('A', (int)actions[2].codepoint);
    wixen_parser_free(&p);
    PASS();
}

SUITE(parser_extended) {
    RUN_TEST(parser_8bit_csi);
    RUN_TEST(parser_sub_cancels);
    RUN_TEST(parser_esc_with_two_intermediates);
    RUN_TEST(parser_csi_semicolons_empty_params);
    RUN_TEST(parser_esc_esc_restarts);
    RUN_TEST(parser_osc_empty_payload);
    RUN_TEST(parser_ground_control_codes);
    RUN_TEST(parser_mixed_utf8_and_csi);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(parser_extended);
    GREATEST_MAIN_END();
}
