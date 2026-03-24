/* test_scaffolding.c — Verify build system works */
#include <stdbool.h>
#include "greatest.h"

TEST scaffolding_builds_and_runs(void) {
    ASSERT_EQ(1, 1);
    PASS();
}

TEST c17_stdbool_works(void) {
    bool flag = true;
    ASSERT(flag);
    flag = false;
    ASSERT_FALSE(flag);
    PASS();
}

SUITE(scaffolding) {
    RUN_TEST(scaffolding_builds_and_runs);
    RUN_TEST(c17_stdbool_works);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(scaffolding);
    GREATEST_MAIN_END();
}
