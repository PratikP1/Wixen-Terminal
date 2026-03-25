/* test_red_tab_lifecycle.c — RED tests for tab manager operations
 *
 * Tabs must support add, remove, rename, reorder, and active tracking.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/ui/tabs.h"

TEST red_tab_add(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Shell", 1);
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    const WixenTab *tab = wixen_tabs_get(&tm, id);
    ASSERT(tab != NULL);
    ASSERT_STR_EQ("Shell", tab->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_add_multiple(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Shell", 1);
    WixenTabId b = wixen_tabs_add(&tm, "Build", 2);
    wixen_tabs_add(&tm, "SSH", 3);
    ASSERT_EQ(3, (int)wixen_tabs_count(&tm));
    const WixenTab *tab = wixen_tabs_get(&tm, b);
    ASSERT_STR_EQ("Build", tab->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_remove(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "A", 1);
    WixenTabId b = wixen_tabs_add(&tm, "B", 2);
    wixen_tabs_add(&tm, "C", 3);
    wixen_tabs_close(&tm, b);
    ASSERT_EQ(2, (int)wixen_tabs_count(&tm));
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_rename(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Shell", 1);
    wixen_tabs_set_title(&tm, id, "PowerShell 7");
    const WixenTab *tab = wixen_tabs_get(&tm, id);
    ASSERT_STR_EQ("PowerShell 7", tab->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_active_tracking(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId a = wixen_tabs_add(&tm, "A", 1);
    WixenTabId b = wixen_tabs_add(&tm, "B", 2);
    const WixenTab *active = wixen_tabs_active(&tm);
    ASSERT(active != NULL);
    wixen_tabs_switch(&tm, b);
    active = wixen_tabs_active(&tm);
    ASSERT(active != NULL);
    ASSERT_EQ(b, active->id);
    (void)a;
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_cycle(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "A", 1);
    wixen_tabs_add(&tm, "B", 2);
    wixen_tabs_add(&tm, "C", 3);
    /* Cycle forward */
    wixen_tabs_cycle(&tm, true);
    const WixenTab *active = wixen_tabs_active(&tm);
    ASSERT(active != NULL);
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_count(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    ASSERT_EQ(0, (int)wixen_tabs_count(&tm));
    wixen_tabs_add(&tm, "A", 1);
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    WixenTabId b = wixen_tabs_add(&tm, "B", 2);
    ASSERT_EQ(2, (int)wixen_tabs_count(&tm));
    wixen_tabs_close(&tm, b);
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    wixen_tabs_free(&tm);
    PASS();
}

TEST red_tab_pane_id(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Shell", 42);
    const WixenTab *tab = wixen_tabs_get(&tm, id);
    ASSERT(tab != NULL);
    ASSERT_EQ(42, (int)tab->active_pane);
    wixen_tabs_free(&tm);
    PASS();
}

SUITE(red_tab_lifecycle) {
    RUN_TEST(red_tab_add);
    RUN_TEST(red_tab_add_multiple);
    RUN_TEST(red_tab_remove);
    RUN_TEST(red_tab_rename);
    RUN_TEST(red_tab_active_tracking);
    RUN_TEST(red_tab_cycle);
    RUN_TEST(red_tab_count);
    RUN_TEST(red_tab_pane_id);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_tab_lifecycle);
    GREATEST_MAIN_END();
}
