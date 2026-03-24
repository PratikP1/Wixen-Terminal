/* test_tabs.c — Tests for tab management */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/tabs.h"

TEST tabs_init_empty(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    ASSERT_EQ(0, (int)wixen_tabs_count(&tm));
    ASSERT(wixen_tabs_active(&tm) == NULL);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_add_one(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Shell 1", 100);
    ASSERT(id > 0);
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    const WixenTab *t = wixen_tabs_active(&tm);
    ASSERT(t != NULL);
    ASSERT_STR_EQ("Shell 1", t->title);
    ASSERT_EQ(100, (int)t->active_pane);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_add_multiple(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Tab1", 1);
    wixen_tabs_add(&tm, "Tab2", 2);
    wixen_tabs_add(&tm, "Tab3", 3);
    ASSERT_EQ(3, (int)wixen_tabs_count(&tm));
    /* Active should be last added */
    ASSERT_STR_EQ("Tab3", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_switch(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id1 = wixen_tabs_add(&tm, "Tab1", 1);
    wixen_tabs_add(&tm, "Tab2", 2);
    ASSERT_STR_EQ("Tab2", wixen_tabs_active(&tm)->title);
    wixen_tabs_switch(&tm, id1);
    ASSERT_STR_EQ("Tab1", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_switch_nonexistent(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Tab1", 1);
    ASSERT_FALSE(wixen_tabs_switch(&tm, 9999));
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_cycle_forward(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Tab1", 1);
    wixen_tabs_add(&tm, "Tab2", 2);
    wixen_tabs_add(&tm, "Tab3", 3);
    wixen_tabs_switch(&tm, tm.tabs[0].id); /* Go to Tab1 */
    wixen_tabs_cycle(&tm, true);
    ASSERT_STR_EQ("Tab2", wixen_tabs_active(&tm)->title);
    wixen_tabs_cycle(&tm, true);
    ASSERT_STR_EQ("Tab3", wixen_tabs_active(&tm)->title);
    wixen_tabs_cycle(&tm, true); /* Wrap */
    ASSERT_STR_EQ("Tab1", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_cycle_backward(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Tab1", 1);
    wixen_tabs_add(&tm, "Tab2", 2);
    wixen_tabs_add(&tm, "Tab3", 3);
    wixen_tabs_switch(&tm, tm.tabs[0].id);
    wixen_tabs_cycle(&tm, false); /* Wrap backward */
    ASSERT_STR_EQ("Tab3", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_cycle_single_tab(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Only", 1);
    ASSERT_FALSE(wixen_tabs_cycle(&tm, true));
    ASSERT_STR_EQ("Only", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_close(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id1 = wixen_tabs_add(&tm, "Tab1", 1);
    wixen_tabs_add(&tm, "Tab2", 2);
    wixen_tabs_close(&tm, id1);
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    ASSERT_STR_EQ("Tab2", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_close_last_tab_fails(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Only", 1);
    ASSERT_FALSE(wixen_tabs_close(&tm, id));
    ASSERT_EQ(1, (int)wixen_tabs_count(&tm));
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_close_active_selects_neighbor(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "Tab1", 1);
    WixenTabId id2 = wixen_tabs_add(&tm, "Tab2", 2);
    wixen_tabs_add(&tm, "Tab3", 3);
    wixen_tabs_switch(&tm, id2);
    wixen_tabs_close(&tm, id2);
    /* Should select a neighbor */
    ASSERT_EQ(2, (int)wixen_tabs_count(&tm));
    ASSERT(wixen_tabs_active(&tm) != NULL);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_set_title(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id = wixen_tabs_add(&tm, "Old", 1);
    wixen_tabs_set_title(&tm, id, "New Title");
    ASSERT_STR_EQ("New Title", wixen_tabs_get(&tm, id)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_get_by_id(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id1 = wixen_tabs_add(&tm, "Tab1", 1);
    WixenTabId id2 = wixen_tabs_add(&tm, "Tab2", 2);
    ASSERT_STR_EQ("Tab1", wixen_tabs_get(&tm, id1)->title);
    ASSERT_STR_EQ("Tab2", wixen_tabs_get(&tm, id2)->title);
    ASSERT(wixen_tabs_get(&tm, 9999) == NULL);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_null_title_defaults(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, NULL, 1);
    ASSERT_STR_EQ("Shell", wixen_tabs_active(&tm)->title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_close_middle_preserves_order(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    wixen_tabs_add(&tm, "A", 1);
    WixenTabId id2 = wixen_tabs_add(&tm, "B", 2);
    wixen_tabs_add(&tm, "C", 3);
    wixen_tabs_close(&tm, id2);
    size_t count;
    const WixenTab *all = wixen_tabs_all(&tm, &count);
    ASSERT_EQ(2, (int)count);
    ASSERT_STR_EQ("A", all[0].title);
    ASSERT_STR_EQ("C", all[1].title);
    wixen_tabs_free(&tm);
    PASS();
}

TEST tabs_ids_are_unique(void) {
    WixenTabManager tm;
    wixen_tabs_init(&tm);
    WixenTabId id1 = wixen_tabs_add(&tm, "A", 1);
    WixenTabId id2 = wixen_tabs_add(&tm, "B", 2);
    WixenTabId id3 = wixen_tabs_add(&tm, "C", 3);
    ASSERT(id1 != id2);
    ASSERT(id2 != id3);
    ASSERT(id1 != id3);
    wixen_tabs_free(&tm);
    PASS();
}

SUITE(tab_lifecycle) {
    RUN_TEST(tabs_init_empty);
    RUN_TEST(tabs_add_one);
    RUN_TEST(tabs_add_multiple);
    RUN_TEST(tabs_null_title_defaults);
    RUN_TEST(tabs_ids_are_unique);
}

SUITE(tab_switching) {
    RUN_TEST(tabs_switch);
    RUN_TEST(tabs_switch_nonexistent);
    RUN_TEST(tabs_cycle_forward);
    RUN_TEST(tabs_cycle_backward);
    RUN_TEST(tabs_cycle_single_tab);
}

SUITE(tab_closing) {
    RUN_TEST(tabs_close);
    RUN_TEST(tabs_close_last_tab_fails);
    RUN_TEST(tabs_close_active_selects_neighbor);
    RUN_TEST(tabs_close_middle_preserves_order);
}

SUITE(tab_properties) {
    RUN_TEST(tabs_set_title);
    RUN_TEST(tabs_get_by_id);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(tab_lifecycle);
    RUN_SUITE(tab_switching);
    RUN_SUITE(tab_closing);
    RUN_SUITE(tab_properties);
    GREATEST_MAIN_END();
}
