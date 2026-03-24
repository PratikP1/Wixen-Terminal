/* test_parser_osc.c — OSC parsing tests */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/vt/parser.h"

static size_t parse(WixenParser *p, const char *data, WixenAction *actions, size_t cap) {
    return wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, cap);
}

static void free_osc(WixenAction *a, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) free(a[i].osc.data);
}

TEST osc_title_bel(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b]0;Hello\x07", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            ASSERT(a[i].osc.data != NULL);
            ASSERT(strstr((char *)a[i].osc.data, "Hello") != NULL ||
                   memcmp(a[i].osc.data, "0;Hello", 7) == 0);
            found = true;
        }
    }
    ASSERT(found);
    free_osc(a, n);
    wixen_parser_free(&p);
    PASS();
}

TEST osc_title_st(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b]2;World\x1b\\", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) found = true;
    }
    ASSERT(found);
    free_osc(a, n);
    wixen_parser_free(&p);
    PASS();
}

TEST osc_empty_payload(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b]0;\x07", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) found = true;
    }
    ASSERT(found);
    free_osc(a, n);
    wixen_parser_free(&p);
    PASS();
}

TEST osc_split_chunks(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n1 = parse(&p, "\x1b]0;Hel", a, 8);
    free_osc(a, n1);
    size_t n2 = parse(&p, "lo\x07", a, 8);
    bool found = false;
    for (size_t i = 0; i < n2; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) found = true;
    }
    ASSERT(found);
    free_osc(a, n2);
    wixen_parser_free(&p);
    PASS();
}

TEST osc_hyperlink_format(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    /* OSC 8 ; params ; uri BEL */
    size_t n = parse(&p, "\x1b]8;id=link1;https://example.com\x07", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            /* Payload should start with "8;" */
            ASSERT(a[i].osc.data[0] == '8');
            found = true;
        }
    }
    ASSERT(found);
    free_osc(a, n);
    wixen_parser_free(&p);
    PASS();
}

TEST osc_133_marker(void) {
    WixenParser p; wixen_parser_init(&p);
    WixenAction a[8];
    size_t n = parse(&p, "\x1b]133;A\x07", a, 8);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        if (a[i].type == WIXEN_ACTION_OSC_DISPATCH) {
            /* Payload starts with "133;" */
            ASSERT(a[i].osc.data_len >= 5);
            found = true;
        }
    }
    ASSERT(found);
    free_osc(a, n);
    wixen_parser_free(&p);
    PASS();
}

SUITE(parser_osc) {
    RUN_TEST(osc_title_bel);
    RUN_TEST(osc_title_st);
    RUN_TEST(osc_empty_payload);
    RUN_TEST(osc_split_chunks);
    RUN_TEST(osc_hyperlink_format);
    RUN_TEST(osc_133_marker);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(parser_osc);
    GREATEST_MAIN_END();
}
