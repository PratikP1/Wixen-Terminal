/* test_red_reduced_motion.c — RED tests for reduced motion config */
#include <stdbool.h>
#include "greatest.h"
#include "wixen/config/config.h"

TEST red_reduced_motion_always(void) {
    ASSERT(wixen_should_reduce_motion(WIXEN_REDUCED_MOTION_ALWAYS, false));
    ASSERT(wixen_should_reduce_motion(WIXEN_REDUCED_MOTION_ALWAYS, true));
    PASS();
}

TEST red_reduced_motion_never(void) {
    ASSERT_FALSE(wixen_should_reduce_motion(WIXEN_REDUCED_MOTION_NEVER, false));
    ASSERT_FALSE(wixen_should_reduce_motion(WIXEN_REDUCED_MOTION_NEVER, true));
    PASS();
}

TEST red_reduced_motion_system(void) {
    /* When set to system, should follow the system preference */
    ASSERT_FALSE(wixen_should_reduce_motion(WIXEN_REDUCED_MOTION_SYSTEM, false));
    ASSERT(wixen_should_reduce_motion(WIXEN_REDUCED_MOTION_SYSTEM, true));
    PASS();
}

SUITE(red_reduced_motion) {
    RUN_TEST(red_reduced_motion_always);
    RUN_TEST(red_reduced_motion_never);
    RUN_TEST(red_reduced_motion_system);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_reduced_motion);
    GREATEST_MAIN_END();
}
