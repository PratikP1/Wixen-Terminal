/* test_pane_zoom.c — Pane zoom and ratio adjustment */
#include <stdbool.h>
#include <math.h>
#include "greatest.h"
#include "wixen/ui/panes.h"

TEST zoom_default_off(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    ASSERT_FALSE(wixen_panes_is_zoomed(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST zoom_on_off(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId child = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    (void)child;
    wixen_panes_zoom(&pt, root);
    ASSERT(wixen_panes_is_zoomed(&pt));
    wixen_panes_unzoom(&pt);
    ASSERT_FALSE(wixen_panes_is_zoomed(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST zoom_layout_single(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId child = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    (void)child;
    wixen_panes_zoom(&pt, root);
    WixenPaneRect rects[16];
    size_t n = wixen_panes_layout(&pt, rects, 16);
    /* When zoomed, only the zoomed pane is laid out at full size */
    ASSERT_EQ(1, (int)n);
    wixen_panes_free(&pt);
    PASS();
}

TEST adjust_ratio(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId child = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    (void)child;
    /* Default ratio is 0.5 */
    bool ok = wixen_panes_adjust_ratio(&pt, root, 0.1f);
    ASSERT(ok);
    /* After adjustment, layout should reflect new ratio */
    WixenPaneRect rects[16];
    size_t n = wixen_panes_layout(&pt, rects, 16);
    ASSERT_EQ(2, (int)n);
    /* First pane should be larger than second */
    ASSERT(rects[0].height > rects[1].height ||
           rects[0].width > rects[1].width);
    wixen_panes_free(&pt);
    PASS();
}

TEST focus_switch(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId child = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    /* Focus on child, then back to root */
    wixen_panes_focus(&pt, child);
    ASSERT_EQ(child, wixen_panes_active(&pt));
    wixen_panes_focus(&pt, root);
    ASSERT_EQ(root, wixen_panes_active(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST double_split(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId p2 = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    WixenPaneId p3 = wixen_panes_split(&pt, p2, WIXEN_SPLIT_VERTICAL);
    (void)p3;
    ASSERT_EQ(3, (int)wixen_panes_count(&pt));
    WixenPaneRect rects[16];
    size_t n = wixen_panes_layout(&pt, rects, 16);
    ASSERT_EQ(3, (int)n);
    wixen_panes_free(&pt);
    PASS();
}

SUITE(pane_zoom) {
    RUN_TEST(zoom_default_off);
    RUN_TEST(zoom_on_off);
    RUN_TEST(zoom_layout_single);
    RUN_TEST(adjust_ratio);
    RUN_TEST(focus_switch);
    RUN_TEST(double_split);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(pane_zoom);
    GREATEST_MAIN_END();
}
