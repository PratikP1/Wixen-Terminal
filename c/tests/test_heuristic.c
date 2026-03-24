/* test_heuristic.c — Tests for heuristic prompt detection */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/shell_integ/heuristic.h"

TEST prompt_cmd(void) {
    ASSERT(wixen_is_prompt_line("C:\\Users\\test>"));
    ASSERT(wixen_is_prompt_line("C:\\Windows\\System32>"));
    PASS();
}

TEST prompt_powershell(void) {
    ASSERT(wixen_is_prompt_line("PS C:\\Users\\test>"));
    ASSERT(wixen_is_prompt_line("PS C:\\>"));
    PASS();
}

TEST prompt_bash_dollar(void) {
    ASSERT(wixen_is_prompt_line("user@host:~$"));
    ASSERT(wixen_is_prompt_line("$ "));
    PASS();
}

TEST prompt_root_hash(void) {
    ASSERT(wixen_is_prompt_line("root@host:~#"));
    ASSERT(wixen_is_prompt_line("# "));
    PASS();
}

TEST prompt_zsh_percent(void) {
    ASSERT(wixen_is_prompt_line("user@host %"));
    PASS();
}

TEST prompt_with_trailing_space(void) {
    ASSERT(wixen_is_prompt_line("C:\\Users\\test> "));
    ASSERT(wixen_is_prompt_line("$ "));
    PASS();
}

TEST not_prompt_output(void) {
    ASSERT_FALSE(wixen_is_prompt_line("Hello World"));
    ASSERT_FALSE(wixen_is_prompt_line("total 42"));
    ASSERT_FALSE(wixen_is_prompt_line("drwxr-xr-x 2 user user 4096 Jan  1 00:00 dir"));
    PASS();
}

TEST not_prompt_empty(void) {
    ASSERT_FALSE(wixen_is_prompt_line(""));
    ASSERT_FALSE(wixen_is_prompt_line(NULL));
    ASSERT_FALSE(wixen_is_prompt_line("   "));
    PASS();
}

TEST prompt_end_col_cmd(void) {
    ASSERT_EQ(15, (int)wixen_prompt_end_col("C:\\Users\\test> ls"));
    PASS();
}

TEST prompt_end_col_dollar(void) {
    ASSERT_EQ(2, (int)wixen_prompt_end_col("$ ls -la"));
    PASS();
}

TEST prompt_end_col_none(void) {
    ASSERT_EQ(0, (int)wixen_prompt_end_col("Hello World"));
    PASS();
}

TEST prompt_end_col_null(void) {
    ASSERT_EQ(0, (int)wixen_prompt_end_col(NULL));
    PASS();
}

SUITE(heuristic_tests) {
    RUN_TEST(prompt_cmd);
    RUN_TEST(prompt_powershell);
    RUN_TEST(prompt_bash_dollar);
    RUN_TEST(prompt_root_hash);
    RUN_TEST(prompt_zsh_percent);
    RUN_TEST(prompt_with_trailing_space);
    RUN_TEST(not_prompt_output);
    RUN_TEST(not_prompt_empty);
    RUN_TEST(prompt_end_col_cmd);
    RUN_TEST(prompt_end_col_dollar);
    RUN_TEST(prompt_end_col_none);
    RUN_TEST(prompt_end_col_null);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(heuristic_tests);
    GREATEST_MAIN_END();
}
