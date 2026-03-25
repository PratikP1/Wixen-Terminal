/* test_red_copilot_p1_focus.c — RED tests for Copilot finding P1
 *
 * P1: Provider focus state not synchronized.
 * HasKeyboardFocus depends on state->has_focus but main loop
 * raises focus events without setting has_focus.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/state.h"

/* P1a: has_focus starts false */
TEST red_p1_focus_initial_false(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_destroy(state);
    PASS();
}

/* P1b: update_focus sets has_focus true */
TEST red_p1_focus_gained(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    wixen_a11y_state_update_focus(state, true);
    ASSERT(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_destroy(state);
    PASS();
}

/* P1c: update_focus sets has_focus false */
TEST red_p1_focus_lost(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    wixen_a11y_state_update_focus(state, true);
    ASSERT(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_update_focus(state, false);
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_destroy(state);
    PASS();
}

/* P1d: has_focus survives multiple toggles */
TEST red_p1_caret_active_matches_focus(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_update_focus(state, true);
    ASSERT(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_update_focus(state, false);
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_update_focus(state, true);
    ASSERT(wixen_a11y_state_has_focus(state));
    wixen_a11y_state_destroy(state);
    PASS();
}

SUITE(red_copilot_p1_focus) {
    RUN_TEST(red_p1_focus_initial_false);
    RUN_TEST(red_p1_focus_gained);
    RUN_TEST(red_p1_focus_lost);
    RUN_TEST(red_p1_caret_active_matches_focus);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_copilot_p1_focus);
    GREATEST_MAIN_END();
}
