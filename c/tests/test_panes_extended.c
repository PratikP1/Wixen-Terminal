/* test_panes_extended.c — Pane tree management tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/ui/panes.h"

TEST pane_init_root(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    ASSERT(root != 0);
    ASSERT_EQ(1, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST pane_split_horizontal(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId child = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT(child != 0);
    ASSERT_EQ(2, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST pane_split_vertical(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId child = wixen_panes_split(&tree, root, WIXEN_SPLIT_VERTICAL);
    ASSERT(child != 0);
    ASSERT_EQ(2, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST pane_close_reduces_count(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    WixenPaneId child = wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    wixen_panes_close(&tree, child);
    ASSERT_EQ(1, (int)wixen_panes_count(&tree));
    wixen_panes_free(&tree);
    PASS();
}

TEST pane_layout_after_split(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    wixen_panes_split(&tree, root, WIXEN_SPLIT_HORIZONTAL);
    WixenPaneRect rects[16];
    size_t n = wixen_panes_layout(&tree, rects, 16);
    ASSERT_EQ(2, (int)n);
    ASSERT(rects[0].width > 0);
    ASSERT(rects[1].width > 0);
    wixen_panes_free(&tree);
    PASS();
}

TEST pane_free_idempotent(void) {
    WixenPaneTree tree;
    WixenPaneId root;
    wixen_panes_init(&tree, &root);
    wixen_panes_free(&tree);
    wixen_panes_free(&tree);
    PASS();
}

SUITE(panes_extended) {
    RUN_TEST(pane_init_root);
    RUN_TEST(pane_split_horizontal);
    RUN_TEST(pane_split_vertical);
    RUN_TEST(pane_close_reduces_count);
    RUN_TEST(pane_layout_after_split);
    RUN_TEST(pane_free_idempotent);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(panes_extended);
    GREATEST_MAIN_END();
}
