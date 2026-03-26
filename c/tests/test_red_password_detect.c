/* test_red_password_detect.c — RED tests for frame-based password prompt detection
 *
 * Bug: "Text not echoed" false positive fires after Enter+dir because
 * Enter (0x0D) sets char_typed_this_frame, and the subsequent shell
 * output frames see no cursor movement from "typing".
 *
 * These tests verify that only printable characters (0x20..0x7E)
 * count as typed-for-echo-purposes, and that Enter resets state.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "greatest.h"
#include "wixen/core/terminal.h"

/* -----------------------------------------------------------------------
 * Test 1: Enter key should NOT trigger password detection.
 * Enter produces WM_CHAR with codepoint 0x0D — it is not printable.
 * ----------------------------------------------------------------------- */
TEST enter_key_not_printable_for_echo(void) {
    ASSERT_FALSE(wixen_is_printable_for_echo(0x0D));
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 2: Regular letter 'a' typed with no cursor movement for 3+ frames
 * SHOULD trigger detection.
 * ----------------------------------------------------------------------- */
TEST printable_char_triggers_password_detect_after_3_frames(void) {
    WixenEchoCheckState s;
    wixen_echo_check_init(&s);

    /* 'a' is printable */
    ASSERT(wixen_is_printable_for_echo('a'));

    /* Simulate: cursor at col 5, user types 'a', but cursor stays at 5
     * (no echo) for 3 consecutive frames. */
    size_t col = 5;

    /* Frame 0: establish baseline column */
    (void)wixen_echo_check_update(&s, true, col, false, false);
    /* First typed frame — col was SIZE_MAX before, so won't match. Resets. */

    /* Frame 1: typed, col unchanged, no cursor move, no output */
    WixenEchoResult r1 = wixen_echo_check_update(&s, true, col, false, false);
    ASSERT_EQ(WIXEN_ECHO_RESULT_NONE, r1);

    /* Frame 2 */
    WixenEchoResult r2 = wixen_echo_check_update(&s, true, col, false, false);
    ASSERT_EQ(WIXEN_ECHO_RESULT_NONE, r2);

    /* Frame 3 — should fire */
    WixenEchoResult r3 = wixen_echo_check_update(&s, true, col, false, false);
    ASSERT_EQ(WIXEN_ECHO_RESULT_PASSWORD, r3);

    PASS();
}

/* -----------------------------------------------------------------------
 * Test 3: Tab key should NOT trigger detection.
 * Tab = 0x09, not a printable character for echo purposes.
 * ----------------------------------------------------------------------- */
TEST tab_key_not_printable_for_echo(void) {
    ASSERT_FALSE(wixen_is_printable_for_echo(0x09));
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 4: Backspace should NOT trigger detection.
 * Backspace = 0x08.
 * ----------------------------------------------------------------------- */
TEST backspace_not_printable_for_echo(void) {
    ASSERT_FALSE(wixen_is_printable_for_echo(0x08));
    PASS();
}

/* -----------------------------------------------------------------------
 * Test 5: Only printable characters (0x20-0x7E) should count.
 * Verify the boundary conditions.
 * ----------------------------------------------------------------------- */
TEST only_printable_range_counts_for_echo(void) {
    /* Below 0x20: control characters — none should count */
    for (uint32_t cp = 0; cp < 0x20; cp++) {
        ASSERT_FALSEm("Control char should not be printable for echo",
                       wixen_is_printable_for_echo(cp));
    }

    /* 0x20 (space) through 0x7E (~) — all should count */
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        ASSERT_EQm("Printable char should count for echo",
                    true, wixen_is_printable_for_echo(cp));
    }

    /* 0x7F (DEL) should NOT count */
    ASSERT_FALSE(wixen_is_printable_for_echo(0x7F));

    /* Escape (0x1B) explicitly should NOT count */
    ASSERT_FALSE(wixen_is_printable_for_echo(0x1B));

    PASS();
}

/* -----------------------------------------------------------------------
 * Test 6: After Enter is pressed (echo_check_reset), shell output should
 * NOT cause a false positive.
 *
 * Scenario from the bug report:
 *   1. User types 'd', 'i', 'r' — echoed normally
 *   2. User presses Enter — NOT a printable char
 *   3. Shell processes command, output arrives (has_real_output)
 *   4. Prompt redraws, cursor moves
 *   5. No "Text not echoed" should fire at any point
 * ----------------------------------------------------------------------- */
TEST enter_then_shell_output_no_false_positive(void) {
    WixenEchoCheckState s;
    wixen_echo_check_init(&s);

    size_t col = 5;

    /* Frames 1-3: user types 'd','i','r' — echoed (cursor moves each time) */
    wixen_echo_check_update(&s, true, col, false, false);  /* baseline */
    col = 6;
    WixenEchoResult r = wixen_echo_check_update(&s, true, col, true, true);
    ASSERT_EQ(WIXEN_ECHO_RESULT_NONE, r);  /* cursor moved = echoed */

    col = 7;
    r = wixen_echo_check_update(&s, true, col, true, true);
    ASSERT_EQ(WIXEN_ECHO_RESULT_NONE, r);

    col = 8;
    r = wixen_echo_check_update(&s, true, col, true, true);
    ASSERT_EQ(WIXEN_ECHO_RESULT_NONE, r);

    /* Enter pressed: NOT a printable char, so char_typed=false.
     * Also reset the echo check state (simulating what main.c should do). */
    wixen_echo_check_reset(&s);

    /* Several frames of shell output after Enter — char_typed=false,
     * but shell produces output and cursor moves around. */
    col = 0; /* prompt redraws at col 0 */
    for (int i = 0; i < 5; i++) {
        r = wixen_echo_check_update(&s, false, col, true, true);
        ASSERT_EQm("Shell output after Enter must not trigger password detect",
                    WIXEN_ECHO_RESULT_NONE, r);
    }

    /* Idle frames after shell finishes — still no false positive */
    for (int i = 0; i < 5; i++) {
        r = wixen_echo_check_update(&s, false, col, false, false);
        ASSERT_EQm("Idle frames after Enter must not trigger password detect",
                    WIXEN_ECHO_RESULT_NONE, r);
    }

    PASS();
}

SUITE(red_password_detect) {
    RUN_TEST(enter_key_not_printable_for_echo);
    RUN_TEST(printable_char_triggers_password_detect_after_3_frames);
    RUN_TEST(tab_key_not_printable_for_echo);
    RUN_TEST(backspace_not_printable_for_echo);
    RUN_TEST(only_printable_range_counts_for_echo);
    RUN_TEST(enter_then_shell_output_no_false_positive);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_password_detect);
    GREATEST_MAIN_END();
}
