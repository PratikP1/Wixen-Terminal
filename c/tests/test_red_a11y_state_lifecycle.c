/* test_red_a11y_state_lifecycle.c — RED-GREEN TDD for P1.3
 *
 * Validates the WixenA11yState lifecycle:
 *   - create / destroy
 *   - double-destroy safety (NULL check)
 *   - focus update via helper matches read
 *   - text update via helper is retrievable
 *   - cursor update via helper matches read
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/state.h"

/* 1. wixen_a11y_state_create returns non-NULL */
TEST lifecycle_create_returns_nonnull(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);
    wixen_a11y_state_destroy(state);
    PASS();
}

/* 2. wixen_a11y_state_destroy frees without leak */
TEST lifecycle_destroy_frees_cleanly(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    ASSERT(state != NULL);
    /* If destroy leaks, an address sanitizer or CRT debug heap will catch it. */
    wixen_a11y_state_destroy(state);
    PASS();
}

/* 3. Double destroy is safe (NULL check) */
TEST lifecycle_double_destroy_safe(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    wixen_a11y_state_destroy(state);
    /* Second destroy with NULL should be a no-op */
    wixen_a11y_state_destroy(NULL);
    PASS();
}

/* 4. Focus update via helper matches direct read */
TEST lifecycle_focus_roundtrip(void) {
    WixenA11yState *state = wixen_a11y_state_create();
    /* Initially false */
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));

    /* Set focus via helper */
    wixen_a11y_state_update_focus(state, true);
    ASSERT(wixen_a11y_state_has_focus(state));

    /* Clear focus via helper */
    wixen_a11y_state_update_focus(state, false);
    ASSERT_FALSE(wixen_a11y_state_has_focus(state));

    wixen_a11y_state_destroy(state);
    PASS();
}

/* 5. Text update via helper is retrievable */
TEST lifecycle_text_roundtrip(void) {
    WixenA11yState *state = wixen_a11y_state_create();

    /* Set text via helper */
    wixen_a11y_state_set_text(state, "hello world");

    /* Read back */
    char buf[64] = {0};
    size_t len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_EQ(11, (int)len);
    ASSERT_STR_EQ("hello world", buf);

    /* Overwrite with new text */
    wixen_a11y_state_set_text(state, "goodbye");
    len = wixen_a11y_state_get_text(state, buf, sizeof(buf));
    ASSERT_EQ(7, (int)len);
    ASSERT_STR_EQ("goodbye", buf);

    wixen_a11y_state_destroy(state);
    PASS();
}

/* 6. Cursor update via helper matches read */
TEST lifecycle_cursor_roundtrip(void) {
    WixenA11yState *state = wixen_a11y_state_create();

    /* Set some multi-line text so cursor offset computation works */
    wixen_a11y_state_set_text(state, "line0\nline1\nline2");

    /* Place cursor at row 1, col 3 (should be offset 6+3 = 9) */
    wixen_a11y_state_update_cursor(state, 1, 3);
    int32_t offset = wixen_a11y_state_cursor_offset(state);
    ASSERT_EQ(9, (int)offset);

    /* Place cursor at row 0, col 0 */
    wixen_a11y_state_update_cursor(state, 0, 0);
    offset = wixen_a11y_state_cursor_offset(state);
    ASSERT_EQ(0, (int)offset);

    wixen_a11y_state_destroy(state);
    PASS();
}

SUITE(a11y_state_lifecycle) {
    RUN_TEST(lifecycle_create_returns_nonnull);
    RUN_TEST(lifecycle_destroy_frees_cleanly);
    RUN_TEST(lifecycle_double_destroy_safe);
    RUN_TEST(lifecycle_focus_roundtrip);
    RUN_TEST(lifecycle_text_roundtrip);
    RUN_TEST(lifecycle_cursor_roundtrip);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(a11y_state_lifecycle);
    GREATEST_MAIN_END();
}

#else
/* Non-Windows stub */
int main(void) { return 0; }
#endif
