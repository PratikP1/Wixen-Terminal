/* test_red_fuzz.c — RED tests: random byte fuzzing
 *
 * Feed random bytes through the parser → terminal pipeline.
 * Must NEVER crash, regardless of input. This is the ultimate
 * C robustness test — every code path must handle arbitrary data.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

/* Simple deterministic PRNG (xorshift32) — reproducible across runs */
static uint32_t fuzz_state = 0xDEADBEEF;
static uint32_t fuzz_next(void) {
    fuzz_state ^= fuzz_state << 13;
    fuzz_state ^= fuzz_state >> 17;
    fuzz_state ^= fuzz_state << 5;
    return fuzz_state;
}

static void feed_bytes(WixenTerminal *t, WixenParser *p,
                        const uint8_t *data, size_t len) {
    WixenAction actions[64];
    size_t pos = 0;
    while (pos < len) {
        size_t chunk = len - pos;
        if (chunk > 256) chunk = 256;
        size_t count = wixen_parser_process(p, data + pos, chunk, actions, 64);
        for (size_t i = 0; i < count; i++) {
            wixen_terminal_dispatch(t, &actions[i]);
            if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH)
                free(actions[i].osc.data);
            if (actions[i].type == WIXEN_ACTION_APC_DISPATCH)
                free(actions[i].apc.data);
        }
        pos += chunk;
    }
}

TEST red_fuzz_random_bytes(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    fuzz_state = 0xDEADBEEF;

    uint8_t buf[4096];
    for (int round = 0; round < 100; round++) {
        size_t len = (fuzz_next() % 4096) + 1;
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(fuzz_next() & 0xFF);
        }
        feed_bytes(&t, &p, buf, len);
    }
    /* If we get here without crashing, PASS */
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_fuzz_escape_heavy(void) {
    /* Random bytes biased toward escape sequences */
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    fuzz_state = 0xCAFEBABE;

    uint8_t buf[2048];
    for (int round = 0; round < 50; round++) {
        size_t len = (fuzz_next() % 2048) + 1;
        for (size_t i = 0; i < len; i++) {
            uint32_t r = fuzz_next();
            if (r % 4 == 0) buf[i] = 0x1B;       /* ESC */
            else if (r % 4 == 1) buf[i] = '[';    /* CSI intro */
            else if (r % 4 == 2) buf[i] = ']';    /* OSC intro */
            else buf[i] = (uint8_t)(r & 0x7F);    /* Printable-ish */
        }
        feed_bytes(&t, &p, buf, len);
    }
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_fuzz_osc_heavy(void) {
    /* Random bytes biased toward long OSC sequences */
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 40, 10);
    wixen_parser_init(&p);
    fuzz_state = 0x12345678;

    for (int round = 0; round < 30; round++) {
        /* Start OSC */
        uint8_t start[] = {0x1B, ']'};
        feed_bytes(&t, &p, start, 2);
        /* Random OSC payload */
        size_t len = (fuzz_next() % 8192) + 1;
        uint8_t *buf = malloc(len);
        if (!buf) continue;
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(0x20 + (fuzz_next() % 95)); /* Printable */
        }
        feed_bytes(&t, &p, buf, len);
        free(buf);
        /* Terminate with BEL */
        uint8_t bel = 0x07;
        feed_bytes(&t, &p, &bel, 1);
    }
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

TEST red_fuzz_resize_during_output(void) {
    /* Interleave random output with resizes */
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    fuzz_state = 0xABCD1234;

    for (int round = 0; round < 50; round++) {
        /* Random output */
        uint8_t buf[512];
        size_t len = (fuzz_next() % 512) + 1;
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(0x20 + (fuzz_next() % 95));
        }
        feed_bytes(&t, &p, buf, len);

        /* Random resize */
        size_t new_cols = (fuzz_next() % 200) + 1;
        size_t new_rows = (fuzz_next() % 100) + 1;
        wixen_terminal_resize(&t, new_cols, new_rows);
    }
    wixen_parser_free(&p);
    wixen_terminal_free(&t);
    PASS();
}

SUITE(red_fuzz) {
    RUN_TEST(red_fuzz_random_bytes);
    RUN_TEST(red_fuzz_escape_heavy);
    RUN_TEST(red_fuzz_osc_heavy);
    RUN_TEST(red_fuzz_resize_during_output);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_fuzz);
    GREATEST_MAIN_END();
}
