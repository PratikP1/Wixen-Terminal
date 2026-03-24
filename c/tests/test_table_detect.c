/* test_table_detect.c — Tests for table detection */
#include <stdbool.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/core/table_detect.h"

TEST table_detect_ls_output(void) {
    /* Data with clear single-space column gaps at consistent positions */
    const char *lines[] = {
        "drwx user group 4096 Jan dir1",
        "drwx user group 2048 Feb dir2",
        "-rwx user group 1024 Mar file",
        "drwx user group 8192 Apr dir3",
    };
    ASSERT(wixen_looks_tabular(lines, 4));
    WixenDetectedTable *t = wixen_detect_table(lines, 4, 0);
    ASSERT(t != NULL);
    ASSERT(t->col_count >= 3);
    wixen_detected_table_free(t);
    PASS();
}

TEST table_detect_not_tabular(void) {
    const char *lines[] = {
        "Compiling wixen v0.1.0",
        "Finished release target",
    };
    ASSERT_FALSE(wixen_looks_tabular(lines, 2));
    PASS();
}

TEST table_detect_single_line(void) {
    const char *lines[] = { "Header1  Header2  Header3" };
    ASSERT_FALSE(wixen_looks_tabular(lines, 1));
    PASS();
}

TEST table_detect_returns_null_for_non_table(void) {
    const char *lines[] = { "abc", "def" };
    WixenDetectedTable *t = wixen_detect_table(lines, 2, 0);
    ASSERT(t == NULL);
    PASS();
}

TEST table_detect_start_row(void) {
    const char *lines[] = {
        "COL1  COL2  COL3",
        "aaa   bbb   ccc ",
        "ddd   eee   fff ",
    };
    WixenDetectedTable *t = wixen_detect_table(lines, 3, 10);
    if (t) {
        ASSERT_EQ(10, (int)t->start_row);
        ASSERT_EQ(13, (int)t->end_row);
        wixen_detected_table_free(t);
    }
    PASS();
}

SUITE(table_detect_tests) {
    RUN_TEST(table_detect_ls_output);
    RUN_TEST(table_detect_not_tabular);
    RUN_TEST(table_detect_single_line);
    RUN_TEST(table_detect_returns_null_for_non_table);
    RUN_TEST(table_detect_start_row);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(table_detect_tests);
    GREATEST_MAIN_END();
}
