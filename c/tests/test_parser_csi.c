/* test_parser_csi.c — CSI sequence parsing edge cases */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/vt/parser.h"

static size_t parse(WixenParser *p, const char *data, WixenAction *actions, size_t cap) {
    return wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, cap);
}

TEST csi_cursor_up(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b[5A", a, 8);
    ASSERT(n >= 1);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'A') {
            ASSERT_EQ(5, (int)a[i].csi.params[0]);
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST csi_default_param(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b[A", a, 8); /* No param = default 1 */
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'A') {
            ASSERT_EQ(0, (int)a[i].csi.params[0]); /* 0 means "use default" */
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST csi_multiple_params(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b[10;20H", a, 8); /* CUP(10,20) */
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'H') {
            ASSERT_EQ(10, (int)a[i].csi.params[0]);
            ASSERT_EQ(20, (int)a[i].csi.params[1]);
            ASSERT_EQ(2, (int)a[i].csi.param_count);
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST csi_private_mode(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b[?25l", a, 8); /* DECTCEM off */
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'l') {
            ASSERT(a[i].csi.intermediates[0] == '?');
            ASSERT_EQ(25, (int)a[i].csi.params[0]);
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST csi_sgr_multi(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b[1;31;42m", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'm') {
            ASSERT_EQ(3, (int)a[i].csi.param_count);
            ASSERT_EQ(1, (int)a[i].csi.params[0]);
            ASSERT_EQ(31, (int)a[i].csi.params[1]);
            ASSERT_EQ(42, (int)a[i].csi.params[2]);
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST csi_split_across_chunks(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    /* Feed ESC first, then [5A in second chunk */
    parse(&p, "\x1b", a, 8);
    size_t n = parse(&p, "[5A", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'A') {
            ASSERT_EQ(5, (int)a[i].csi.params[0]);
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST csi_many_params(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b[1;2;3;4;5;6;7;8m", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_CSI_DISPATCH && a[i].csi.final_byte == 'm') {
            ASSERT(a[i].csi.param_count >= 8);
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST osc_title_parsed(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b]0;My Title\x07", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            ASSERT(a[i].osc.data != NULL);
            found = true;
            free(a[i].osc.data);
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST esc_ri(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1bM", a, 8); /* RI (Reverse Index) */
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_ESC_DISPATCH && a[i].esc.final_byte == 'M') {
            found = true;
        }
    }
    ASSERT(found);
    wixen_parser_free(&p);
    PASS();
}

TEST plain_text_passthrough(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[32];
    size_t n = parse(&p, "Hello", a, 32);
    /* Should get 5 PRINT actions */
    size_t prints = 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_PRINT) prints++;
    }
    ASSERT_EQ(5, (int)prints);
    wixen_parser_free(&p);
    PASS();
}

SUITE(parser_csi) {
    RUN_TEST(csi_cursor_up);
    RUN_TEST(csi_default_param);
    RUN_TEST(csi_multiple_params);
    RUN_TEST(csi_private_mode);
    RUN_TEST(csi_sgr_multi);
    RUN_TEST(csi_split_across_chunks);
    RUN_TEST(csi_many_params);
    RUN_TEST(osc_title_parsed);
    RUN_TEST(esc_ri);
    RUN_TEST(plain_text_passthrough);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(parser_csi);
    GREATEST_MAIN_END();
}
