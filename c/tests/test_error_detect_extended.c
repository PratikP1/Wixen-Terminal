/* test_error_detect_extended.c — More error detection edge cases */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/error_detect.h"

TEST error_python_traceback(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("Traceback (most recent call last):"));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("  File \"main.py\", line 42, in <module>"));
    PASS();
}

TEST error_npm_failed(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("npm ERR! code ELIFECYCLE"));
    PASS();
}

TEST error_rust_compiler(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("error[E0308]: mismatched types"));
    PASS();
}

TEST warning_rust_unused(void) {
    ASSERT_EQ(WIXEN_LINE_WARNING, wixen_classify_output_line("warning: unused variable: `x`"));
    PASS();
}

TEST info_cargo_note(void) {
    ASSERT_EQ(WIXEN_LINE_INFO, wixen_classify_output_line("note: `#[warn(unused_variables)]` on by default"));
    PASS();
}

TEST progress_npm_install(void) {
    ASSERT_EQ(75, wixen_detect_progress("75% ████████████████░░░░"));
    PASS();
}

TEST progress_pip(void) {
    ASSERT_EQ(100, wixen_detect_progress("Installing collected packages... 100%"));
    PASS();
}

TEST progress_cmake(void) {
    int p = wixen_detect_progress("[ 50%] Building C object main.c.o");
    ASSERT_EQ(50, p);
    PASS();
}

TEST error_java_exception(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR,
        wixen_classify_output_line("Exception in thread \"main\" java.lang.NullPointerException"));
    PASS();
}

TEST normal_git_output(void) {
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("On branch main"));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("nothing to commit, working tree clean"));
    PASS();
}

TEST count_zero_lines(void) {
    size_t e, w;
    wixen_count_errors_warnings(NULL, 0, &e, &w);
    ASSERT_EQ(0, (int)e);
    ASSERT_EQ(0, (int)w);
    PASS();
}

TEST count_all_normal(void) {
    const char *lines[] = {"hello", "world", "foo"};
    size_t e, w;
    wixen_count_errors_warnings(lines, 3, &e, &w);
    ASSERT_EQ(0, (int)e);
    ASSERT_EQ(0, (int)w);
    PASS();
}

TEST progress_negative_result(void) {
    ASSERT_EQ(-1, wixen_detect_progress("no progress here"));
    ASSERT_EQ(-1, wixen_detect_progress("abc%def")); /* % not after number */
    PASS();
}

SUITE(error_detect_extended) {
    RUN_TEST(error_python_traceback);
    RUN_TEST(error_npm_failed);
    RUN_TEST(error_rust_compiler);
    RUN_TEST(warning_rust_unused);
    RUN_TEST(info_cargo_note);
    RUN_TEST(progress_npm_install);
    RUN_TEST(progress_pip);
    RUN_TEST(progress_cmake);
    RUN_TEST(error_java_exception);
    RUN_TEST(normal_git_output);
    RUN_TEST(count_zero_lines);
    RUN_TEST(count_all_normal);
    RUN_TEST(progress_negative_result);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(error_detect_extended);
    GREATEST_MAIN_END();
}
