/* test_error_detect.c — Tests for output line classification */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/core/error_detect.h"

/* === Classification === */

TEST classify_normal_text(void) {
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("Hello World"));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("Compiling foo..."));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line(""));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line(NULL));
    PASS();
}

TEST classify_error_keyword(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("error: something failed"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("Error: bad input"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("ERROR: crash"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("FAILED to compile"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("fatal error: file not found"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("thread 'main' panicked at"));
    PASS();
}

TEST classify_rust_error(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR,
        wixen_classify_output_line("error[E0308]: mismatched types"));
    PASS();
}

TEST classify_msvc_error(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR,
        wixen_classify_output_line("main.c(10): error C2220: warning treated as error"));
    PASS();
}

TEST classify_gcc_error(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR,
        wixen_classify_output_line("main.c:10:5: error: expected ';'"));
    PASS();
}

TEST classify_caret_line(void) {
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("    ^^^"));
    ASSERT_EQ(WIXEN_LINE_ERROR, wixen_classify_output_line("  ^^^^~~~"));
    PASS();
}

TEST classify_caret_not_false_positive(void) {
    /* "^" alone or mixed with text shouldn't be error */
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("^"));
    ASSERT_EQ(WIXEN_LINE_NORMAL, wixen_classify_output_line("^abc"));
    PASS();
}

TEST classify_warning(void) {
    ASSERT_EQ(WIXEN_LINE_WARNING, wixen_classify_output_line("warning: unused variable"));
    ASSERT_EQ(WIXEN_LINE_WARNING, wixen_classify_output_line("Warning: slow query"));
    ASSERT_EQ(WIXEN_LINE_WARNING, wixen_classify_output_line("[WARN] memory low"));
    ASSERT_EQ(WIXEN_LINE_WARNING, wixen_classify_output_line("deprecated: use foo_v2"));
    PASS();
}

TEST classify_info(void) {
    ASSERT_EQ(WIXEN_LINE_INFO, wixen_classify_output_line("note: expected &str"));
    ASSERT_EQ(WIXEN_LINE_INFO, wixen_classify_output_line("info: build complete"));
    ASSERT_EQ(WIXEN_LINE_INFO, wixen_classify_output_line("hint: try adding #[derive]"));
    PASS();
}

TEST classify_error_in_path_not_false_positive(void) {
    /* "error" inside a path shouldn't trigger */
    ASSERT_EQ(WIXEN_LINE_NORMAL,
        wixen_classify_output_line("/home/user/error_handler.rs"));
    PASS();
}

/* === Counting === */

TEST count_mixed(void) {
    const char *lines[] = {
        "Compiling...",
        "error: bad",
        "warning: slow",
        "error: worse",
        "done",
    };
    size_t errors, warnings;
    wixen_count_errors_warnings(lines, 5, &errors, &warnings);
    ASSERT_EQ(2, (int)errors);
    ASSERT_EQ(1, (int)warnings);
    PASS();
}

TEST count_empty(void) {
    size_t errors, warnings;
    wixen_count_errors_warnings(NULL, 0, &errors, &warnings);
    ASSERT_EQ(0, (int)errors);
    ASSERT_EQ(0, (int)warnings);
    PASS();
}

/* === Progress === */

TEST progress_percentage(void) {
    ASSERT_EQ(50, wixen_detect_progress("Downloading 50%"));
    ASSERT_EQ(100, wixen_detect_progress("100% complete"));
    ASSERT_EQ(0, wixen_detect_progress("0% started"));
    PASS();
}

TEST progress_fraction(void) {
    ASSERT_EQ(50, wixen_detect_progress("5/10 files"));
    ASSERT_EQ(33, wixen_detect_progress("1/3 done"));
    ASSERT_EQ(100, wixen_detect_progress("10/10"));
    PASS();
}

TEST progress_of_pattern(void) {
    ASSERT_EQ(50, wixen_detect_progress("5 of 10 tests"));
    ASSERT_EQ(75, wixen_detect_progress("3 of 4 passed"));
    PASS();
}

TEST progress_none(void) {
    ASSERT_EQ(-1, wixen_detect_progress("Hello World"));
    ASSERT_EQ(-1, wixen_detect_progress(""));
    ASSERT_EQ(-1, wixen_detect_progress(NULL));
    PASS();
}

TEST progress_over_100_ignored(void) {
    /* 200% shouldn't match as valid progress */
    ASSERT_EQ(-1, wixen_detect_progress("200% complete"));
    PASS();
}

SUITE(classification) {
    RUN_TEST(classify_normal_text);
    RUN_TEST(classify_error_keyword);
    RUN_TEST(classify_rust_error);
    RUN_TEST(classify_msvc_error);
    RUN_TEST(classify_gcc_error);
    RUN_TEST(classify_caret_line);
    RUN_TEST(classify_caret_not_false_positive);
    RUN_TEST(classify_warning);
    RUN_TEST(classify_info);
    RUN_TEST(classify_error_in_path_not_false_positive);
}

SUITE(counting) {
    RUN_TEST(count_mixed);
    RUN_TEST(count_empty);
}

SUITE(progress_detection) {
    RUN_TEST(progress_percentage);
    RUN_TEST(progress_fraction);
    RUN_TEST(progress_of_pattern);
    RUN_TEST(progress_none);
    RUN_TEST(progress_over_100_ignored);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(classification);
    RUN_SUITE(counting);
    RUN_SUITE(progress_detection);
    GREATEST_MAIN_END();
}
