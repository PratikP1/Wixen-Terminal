/* test_red_prompt_strip.c — RED tests for shell prompt stripping
 *
 * When announcing history recall or current line to screen readers,
 * the shell prompt (e.g., "C:\Users\prati>" or "PS C:\>") should
 * be stripped so only the command text is spoken.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/events.h"

TEST red_strip_cmd_prompt(void) {
    char *text = wixen_strip_prompt("C:\\Users\\prati>dir /s");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("dir /s", text);
    free(text);
    PASS();
}

TEST red_strip_ps_prompt(void) {
    char *text = wixen_strip_prompt("PS C:\\Projects> git status");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("git status", text);
    free(text);
    PASS();
}

TEST red_strip_dollar_prompt(void) {
    char *text = wixen_strip_prompt("$ ls -la");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("ls -la", text);
    free(text);
    PASS();
}

TEST red_strip_hash_prompt(void) {
    char *text = wixen_strip_prompt("# apt update");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("apt update", text);
    free(text);
    PASS();
}

TEST red_strip_no_prompt(void) {
    /* Text without a recognizable prompt — return as-is */
    char *text = wixen_strip_prompt("just some text");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("just some text", text);
    free(text);
    PASS();
}

TEST red_strip_empty(void) {
    char *text = wixen_strip_prompt("");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("", text);
    free(text);
    PASS();
}

TEST red_strip_null(void) {
    char *text = wixen_strip_prompt(NULL);
    ASSERT(text != NULL);
    ASSERT_STR_EQ("", text);
    free(text);
    PASS();
}

TEST red_strip_prompt_only(void) {
    /* Just a prompt with no command */
    char *text = wixen_strip_prompt("C:\\Users\\prati>");
    ASSERT(text != NULL);
    ASSERT_STR_EQ("", text);
    free(text);
    PASS();
}

SUITE(red_prompt_strip) {
    RUN_TEST(red_strip_cmd_prompt);
    RUN_TEST(red_strip_ps_prompt);
    RUN_TEST(red_strip_dollar_prompt);
    RUN_TEST(red_strip_hash_prompt);
    RUN_TEST(red_strip_no_prompt);
    RUN_TEST(red_strip_empty);
    RUN_TEST(red_strip_null);
    RUN_TEST(red_strip_prompt_only);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_prompt_strip);
    GREATEST_MAIN_END();
}
