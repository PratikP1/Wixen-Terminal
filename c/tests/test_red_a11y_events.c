/* test_red_a11y_events.c — RED tests for 7 missing a11y event functions
 *
 * These functions exist in Rust but not yet in C:
 * - raise_text_selection_changed
 * - raise_text_changed
 * - raise_live_region_changed
 * - raise_focus_changed
 * - raise_command_complete
 * - raise_mode_change
 * - raise_image_placed
 *
 * Since UIA events require a real HWND + provider, we test the helper
 * wrappers that format the notification text and validate parameters.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/events.h"

/* === raise_command_complete formatting === */

TEST red_command_complete_format_success(void) {
    char *text = wixen_a11y_format_command_complete("git push", 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "git push") != NULL);
    ASSERT(strstr(text, "succeeded") != NULL || strstr(text, "0") != NULL);
    free(text);
    PASS();
}

TEST red_command_complete_format_failure(void) {
    char *text = wixen_a11y_format_command_complete("cargo build", 1);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "cargo build") != NULL);
    ASSERT(strstr(text, "failed") != NULL || strstr(text, "1") != NULL);
    free(text);
    PASS();
}

TEST red_command_complete_null_command(void) {
    char *text = wixen_a11y_format_command_complete(NULL, 0);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "command") != NULL || strlen(text) > 0);
    free(text);
    PASS();
}

/* === raise_mode_change formatting === */

TEST red_mode_change_format_enabled(void) {
    char *text = wixen_a11y_format_mode_change("zoom", true);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "zoom") != NULL);
    ASSERT(strstr(text, "on") != NULL || strstr(text, "enabled") != NULL);
    free(text);
    PASS();
}

TEST red_mode_change_format_disabled(void) {
    char *text = wixen_a11y_format_mode_change("broadcast", false);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "broadcast") != NULL);
    ASSERT(strstr(text, "off") != NULL || strstr(text, "disabled") != NULL);
    free(text);
    PASS();
}

/* === raise_image_placed formatting === */

TEST red_image_placed_format(void) {
    char *text = wixen_a11y_format_image_placed(80, 24, "screenshot.png");
    ASSERT(text != NULL);
    ASSERT(strstr(text, "image") != NULL || strstr(text, "Image") != NULL);
    ASSERT(strstr(text, "80") != NULL);
    free(text);
    PASS();
}

TEST red_image_placed_no_name(void) {
    char *text = wixen_a11y_format_image_placed(40, 10, NULL);
    ASSERT(text != NULL);
    ASSERT(strstr(text, "image") != NULL || strstr(text, "Image") != NULL);
    free(text);
    PASS();
}

SUITE(red_a11y_events) {
    RUN_TEST(red_command_complete_format_success);
    RUN_TEST(red_command_complete_format_failure);
    RUN_TEST(red_command_complete_null_command);
    RUN_TEST(red_mode_change_format_enabled);
    RUN_TEST(red_mode_change_format_disabled);
    RUN_TEST(red_image_placed_format);
    RUN_TEST(red_image_placed_no_name);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_a11y_events);
    GREATEST_MAIN_END();
}
