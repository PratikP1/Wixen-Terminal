/* test_red_path_safety.c — RED tests for safe session path rewriting
 *
 * The config path needs to be rewritten to a session path:
 *   config.toml -> config.session.json
 * This must be bounded and never overflow.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/path_util.h"

TEST red_path_normal_extension(void) {
    char out[256];
    bool ok = wixen_session_path_from_config("C:\\Users\\test\\config.toml", out, sizeof(out));
    ASSERT(ok);
    ASSERT_STR_EQ("C:\\Users\\test\\config.session.json", out);
    PASS();
}

TEST red_path_no_extension(void) {
    char out[256];
    bool ok = wixen_session_path_from_config("C:\\Users\\test\\config", out, sizeof(out));
    ASSERT(ok);
    ASSERT_STR_EQ("C:\\Users\\test\\config.session.json", out);
    PASS();
}

TEST red_path_empty_input(void) {
    char out[256];
    bool ok = wixen_session_path_from_config("", out, sizeof(out));
    ASSERT_FALSE(ok);
    PASS();
}

TEST red_path_null_input(void) {
    char out[256];
    bool ok = wixen_session_path_from_config(NULL, out, sizeof(out));
    ASSERT_FALSE(ok);
    PASS();
}

TEST red_path_buffer_too_small(void) {
    char out[10]; /* Too small for any real path */
    bool ok = wixen_session_path_from_config("C:\\config.toml", out, sizeof(out));
    ASSERT_FALSE(ok);
    PASS();
}

TEST red_path_exact_fit(void) {
    /* "a.toml" -> "a.session.json" = 14 chars + null = 15 */
    char out[15];
    bool ok = wixen_session_path_from_config("a.toml", out, sizeof(out));
    ASSERT(ok);
    ASSERT_STR_EQ("a.session.json", out);
    PASS();
}

TEST red_path_one_byte_short(void) {
    /* "a.toml" -> "a.session.json" needs 15 bytes, give 14 */
    char out[14];
    bool ok = wixen_session_path_from_config("a.toml", out, sizeof(out));
    ASSERT_FALSE(ok);
    PASS();
}

TEST red_path_long_input(void) {
    /* Near MAX_PATH */
    char input[260];
    memset(input, 'x', 240);
    memcpy(input + 240, ".toml", 5);
    input[245] = '\0';
    char out[300];
    bool ok = wixen_session_path_from_config(input, out, sizeof(out));
    ASSERT(ok);
    ASSERT(strstr(out, ".session.json") != NULL);
    /* Verify the base is preserved */
    ASSERT(memcmp(out, input, 240) == 0);
    PASS();
}

TEST red_path_null_terminated(void) {
    char out[256];
    memset(out, 'Z', sizeof(out));
    bool ok = wixen_session_path_from_config("test.toml", out, sizeof(out));
    ASSERT(ok);
    ASSERT_EQ('\0', out[strlen(out)]);
    PASS();
}

SUITE(red_path_safety) {
    RUN_TEST(red_path_normal_extension);
    RUN_TEST(red_path_no_extension);
    RUN_TEST(red_path_empty_input);
    RUN_TEST(red_path_null_input);
    RUN_TEST(red_path_buffer_too_small);
    RUN_TEST(red_path_exact_fit);
    RUN_TEST(red_path_one_byte_short);
    RUN_TEST(red_path_long_input);
    RUN_TEST(red_path_null_terminated);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_path_safety);
    GREATEST_MAIN_END();
}
