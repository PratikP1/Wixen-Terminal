/* test_tabs_extended.c — Additional tab management tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/tabs.h"

TEST tabs_initial_empty(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    ASSERT_EQ(0, (int)wixen_tabs_count(&tm));
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_add_three(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id1 = wixen_tabs_add(&tm, "Shell 1", 1);
    wixen_tabs_add(&tm, "Shell 2", 2);
    WixenTabId id3 = wixen_tabs_add(&tm, "Shell 3", 3);
    ASSERT_EQ(3, (int)wixen_tabs_count(&tm));
    ASSERT_STR_EQ("Shell 1", wixen_tabs_get(&tm, id1)->title);
    ASSERT_STR_EQ("Shell 3", wixen_tabs_get(&tm, id3)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_close_last(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId a = wixen_tabs_add(&tm, "A", 1);
    wixen_tabs_add(&tm, "B", 2);
    ASSERT_EQ(2, (int)wixen_tabs_count(&tm));
    /* Close first tab — should switch to next */
    wixen_tabs_close(&tm, a);
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    ASSERT_STR_EQ("B", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_active_default(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "First", 1);
    const WixenTab *active = wixen_tabs_active(&tm);
    ASSERT(active != NULL);
    ASSERT_STR_EQ("First", active->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_switch_active(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "A", 1);
    WixenTabId b = wixen_tabs_add(&tm, "B", 2);
    wixen_tabs_switch(&tm, b);
    ASSERT_STR_EQ("B", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_set_title(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Old Name", 1);
    wixen_tabs_set_title(&tm, id, "New Name");
    ASSERT_STR_EQ("New Name", wixen_tabs_get(&tm, id)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_cycle_forward(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "A", 1);
    WixenTabId b = wixen_tabs_add(&tm, "B", 2);
    wixen_tabs_add(&tm, "C", 3);
    /* Switch to B first, then cycle forward to C */
    wixen_tabs_switch(&tm, b);
    ASSERT_STR_EQ("B", wixen_tabs_active(&tm)->title);
    wixen_tabs_cycle(&tm, true);
    ASSERT_STR_EQ("C", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_free_idempotent(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "X", 1);
    wixen_tabs_free(&tm);
    wixen_tabs_free(&tm);
    PASS();
}

SUITE(tabs_extended) {
    RUN_TEST(tabs_initial_empty);
    RUN_TEST(tabs_add_three);
    RUN_TEST(tabs_close_last);
    RUN_TEST(tabs_active_default);
    RUN_TEST(tabs_switch_active);
    RUN_TEST(tabs_set_title);
    RUN_TEST(tabs_cycle_forward);
    RUN_TEST(tabs_free_idempotent);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(tabs_extended);
    GREATEST_MAIN_END();
}
