/* test_red_clipboard.c — RED tests for OSC 52 clipboard integration
 *
 * OSC 52 allows terminal applications to read/write the system clipboard.
 * Format: ESC ] 52 ; <target> ; <base64-data> BEL
 * target: c = clipboard, p = primary selection
 * data: ? = query (read), base64 = write
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/terminal.h"
#include "wixen/vt/parser.h"

static void feed(WixenTerminal *t, WixenParser *p, const char *data) {
    WixenAction actions[512];
    size_t count = wixen_parser_process(p, (const uint8_t *)data, strlen(data), actions, 512);
    for (size_t i = 0; i < count; i++) {
        wixen_terminal_dispatch(t, &actions[i]);
        if (actions[i].type == WIXEN_ACTION_OSC_DISPATCH) free(actions[i].osc.data);
    }
}

/* === Base64 encoding/decoding === */

TEST red_base64_encode(void) {
    char *encoded = wixen_base64_encode((const uint8_t *)"Hello", 5);
    ASSERT(encoded != NULL);
    ASSERT_STR_EQ("SGVsbG8=", encoded);
    free(encoded);
    PASS();
}

TEST red_base64_encode_empty(void) {
    char *encoded = wixen_base64_encode((const uint8_t *)"", 0);
    ASSERT(encoded != NULL);
    ASSERT_STR_EQ("", encoded);
    free(encoded);
    PASS();
}

TEST red_base64_decode(void) {
    size_t out_len = 0;
    uint8_t *decoded = wixen_base64_decode("SGVsbG8=", &out_len);
    ASSERT(decoded != NULL);
    ASSERT_EQ(5, (int)out_len);
    ASSERT(memcmp(decoded, "Hello", 5) == 0);
    free(decoded);
    PASS();
}

TEST red_base64_decode_no_padding(void) {
    size_t out_len = 0;
    uint8_t *decoded = wixen_base64_decode("SGVsbG8", &out_len);
    ASSERT(decoded != NULL);
    ASSERT_EQ(5, (int)out_len);
    ASSERT(memcmp(decoded, "Hello", 5) == 0);
    free(decoded);
    PASS();
}

TEST red_base64_roundtrip(void) {
    const char *original = "The quick brown fox jumps over the lazy dog";
    char *encoded = wixen_base64_encode((const uint8_t *)original, strlen(original));
    size_t out_len = 0;
    uint8_t *decoded = wixen_base64_decode(encoded, &out_len);
    ASSERT_EQ(strlen(original), out_len);
    ASSERT(memcmp(decoded, original, out_len) == 0);
    free(encoded);
    free(decoded);
    PASS();
}

/* === OSC 52 clipboard write === */

TEST red_osc52_write_queues(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* OSC 52 ; c ; SGVsbG8= BEL  (write "Hello" to clipboard) */
    feed(&t, &p, "\x1b]52;c;SGVsbG8=\x07");
    /* Should have queued a clipboard write */
    char *clip = wixen_terminal_drain_clipboard_write(&t);
    ASSERT(clip != NULL);
    ASSERT_STR_EQ("Hello", clip);
    free(clip);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_osc52_query_generates_response(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* Inject clipboard content for query response */
    wixen_terminal_inject_clipboard(&t, "World");
    /* OSC 52 ; c ; ? BEL  (query clipboard) */
    feed(&t, &p, "\x1b]52;c;?\x07");
    /* Should have generated an OSC 52 response with base64 of "World" */
    const char *resp = wixen_terminal_pop_response(&t);
    ASSERT(resp != NULL);
    ASSERT(strstr(resp, "52;c;") != NULL);
    ASSERT(strstr(resp, "V29ybGQ=") != NULL); /* base64("World") */
    free((void *)resp);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

TEST red_osc52_empty_clears(void) {
    WixenTerminal t; WixenParser p;
    wixen_terminal_init(&t, 80, 24);
    wixen_parser_init(&p);
    /* OSC 52 ; c ; BEL  (empty data = clear clipboard) */
    feed(&t, &p, "\x1b]52;c;\x07");
    char *clip = wixen_terminal_drain_clipboard_write(&t);
    /* Should get empty string (clear) */
    ASSERT(clip != NULL);
    ASSERT_EQ(0, (int)strlen(clip));
    free(clip);
    wixen_parser_free(&p); wixen_terminal_free(&t);
    PASS();
}

SUITE(red_clipboard) {
    RUN_TEST(red_base64_encode);
    RUN_TEST(red_base64_encode_empty);
    RUN_TEST(red_base64_decode);
    RUN_TEST(red_base64_decode_no_padding);
    RUN_TEST(red_base64_roundtrip);
    RUN_TEST(red_osc52_write_queues);
    RUN_TEST(red_osc52_query_generates_response);
    RUN_TEST(red_osc52_empty_clears);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_clipboard);
    GREATEST_MAIN_END();
}
