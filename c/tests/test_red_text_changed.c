/* test_red_text_changed.c — RED-GREEN TDD for UIA TextChanged event logic
 *
 * Tests the pure-logic helper wixen_a11y_should_raise_text_changed()
 * which decides whether a UIA_Text_TextChangedEventId should be raised
 * after a frame update. This avoids needing COM for testing.
 */
#include "greatest.h"
#include "wixen/a11y/events.h"

/* --- Test 1: Text changes between frames => should raise --- */
TEST text_changes_between_frames(void) {
    bool result = wixen_a11y_should_raise_text_changed("hello world", "hello changed");
    ASSERT(result);
    PASS();
}

/* --- Test 2: Text stays the same => should NOT raise --- */
TEST text_stays_same_no_raise(void) {
    bool result = wixen_a11y_should_raise_text_changed("hello world", "hello world");
    ASSERT_FALSE(result);
    PASS();
}

/* --- Test 3: Empty to non-empty => should raise --- */
TEST empty_to_nonempty_triggers(void) {
    /* NULL old text to non-empty new text */
    ASSERT(wixen_a11y_should_raise_text_changed(NULL, "new content"));
    /* Empty string old text to non-empty new text */
    ASSERT(wixen_a11y_should_raise_text_changed("", "new content"));
    PASS();
}

/* --- Test 4: Non-empty to empty => should raise --- */
TEST nonempty_to_empty_triggers(void) {
    /* Non-empty to NULL */
    ASSERT(wixen_a11y_should_raise_text_changed("old content", NULL));
    /* Non-empty to empty string */
    ASSERT(wixen_a11y_should_raise_text_changed("old content", ""));
    PASS();
}

/* --- Test 5: Both NULL/empty (cursor-only move) => should NOT raise --- */
TEST both_null_no_raise(void) {
    ASSERT_FALSE(wixen_a11y_should_raise_text_changed(NULL, NULL));
    ASSERT_FALSE(wixen_a11y_should_raise_text_changed("", ""));
    ASSERT_FALSE(wixen_a11y_should_raise_text_changed(NULL, ""));
    ASSERT_FALSE(wixen_a11y_should_raise_text_changed("", NULL));
    PASS();
}

SUITE(text_changed) {
    RUN_TEST(text_changes_between_frames);
    RUN_TEST(text_stays_same_no_raise);
    RUN_TEST(empty_to_nonempty_triggers);
    RUN_TEST(nonempty_to_empty_triggers);
    RUN_TEST(both_null_no_raise);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(text_changed);
    GREATEST_MAIN_END();
}
