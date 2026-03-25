/* test_red_pane_tree.c — RED tests for pane split tree
 *
 * Panes can be split horizontally/vertically, navigated,
 * and zoomed. The tree tracks layout ratios.
 */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/panes.h"

TEST red_pane_init(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    ASSERT(root > 0);
    ASSERT_EQ(1, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST red_pane_split_horizontal(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId new_pane = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT(new_pane > 0);
    ASSERT(new_pane != root);
    ASSERT_EQ(2, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST red_pane_split_vertical(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId new_pane = wixen_panes_split(&tree, root, WIXEN_SPLIT_VERTICAL);
    ASSERT(new_pane > 0);
    ASSERT_EQ(2, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST red_pane_close(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId second = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT_EQ(2, (int)wixen_panes_count(&tree));
    wixen_panes_close(&tree, second);
    ASSERT_EQ(1, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST red_pane_focus(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId second = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    /* Split moves focus to new pane */
    ASSERT_EQ(second, wixen_panes_active(&tree));
    /* Focus back to root */
    wixen_panes_focus(&tree, root);
    ASSERT_EQ(root, wixen_panes_active(&tree));
    /* Focus to second */
    wixen_panes_focus(&tree, second);
    ASSERT_EQ(second, wixen_panes_active(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST red_pane_zoom(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId second = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT_FALSE(wixen_panes_is_zoomed(&tree));
    wixen_panes_zoom(&tree, root);
    ASSERT(wixen_panes_is_zoomed(&tree));
    wixen_panes_unzoom(&tree);
    ASSERT_FALSE(wixen_panes_is_zoomed(&tree));
    (void)second;
    wixen_panes_free(&tree);
    PASS();
}

TEST red_pane_layout_bounds(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId second = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    /* Get layout for all panes */
    WixenPaneRect rects[8];
    size_t count = wixen_panes_layout(&tree, rects, 8);
    ASSERT(count >= 2);
    /* Both panes should have positive dimensions */
    bool found_root = false, found_second = false;
    for (size_t i = 0; i < count; i++) {
        if (rects[i].pane_id == root) { found_root = true; ASSERT(rects[i].width > 0); }
        if (rects[i].pane_id == second) { found_second = true; ASSERT(rects[i].width > 0); }
    }
    ASSERT(found_root);
    ASSERT(found_second);
    wixen_panes_free(&tree);
    PASS();
}

SUITE(red_pane_tree) {
    RUN_TEST(red_pane_init);
    RUN_TEST(red_pane_split_horizontal);
    RUN_TEST(red_pane_split_vertical);
    RUN_TEST(red_pane_close);
    RUN_TEST(red_pane_focus);
    RUN_TEST(red_pane_zoom);
    RUN_TEST(red_pane_layout_bounds);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_pane_tree);
    GREATEST_MAIN_END();
}
