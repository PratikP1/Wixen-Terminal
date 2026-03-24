/* test_panes.c — Tests for pane split tree */
#include <stdbool.h>
#include <math.h>
#include "greatest.h"
#include "wixen/ui/panes.h"

#define APPROX_EQ(a, b) (fabs((double)(a) - (double)(b)) < 0.01)

TEST panes_init_single(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    ASSERT(root > 0);
    ASSERT_EQ(1, (int)wixen_panes_count(&pt));
    ASSERT_EQ(root, wixen_panes_active(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_layout_single(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneRect rects[4];
    size_t count = wixen_panes_layout(&pt, rects, 4);
    ASSERT_EQ(1, (int)count);
    ASSERT(APPROX_EQ(0, rects[0].x));
    ASSERT(APPROX_EQ(0, rects[0].y));
    ASSERT(APPROX_EQ(1, rects[0].width));
    ASSERT(APPROX_EQ(1, rects[0].height));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_split_horizontal(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId new_id = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT(new_id > 0);
    ASSERT_EQ(2, (int)wixen_panes_count(&pt));
    WixenPaneRect rects[4];
    size_t count = wixen_panes_layout(&pt, rects, 4);
    ASSERT_EQ(2, (int)count);
    /* Left pane: x=0, width=0.5 */
    ASSERT(APPROX_EQ(0, rects[0].x));
    ASSERT(APPROX_EQ(0.5, rects[0].width));
    /* Right pane: x=0.5, width=0.5 */
    ASSERT(APPROX_EQ(0.5, rects[1].x));
    ASSERT(APPROX_EQ(0.5, rects[1].width));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_split_vertical(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId new_id = wixen_panes_split(&pt, root, WIXEN_SPLIT_VERTICAL);
    ASSERT(new_id > 0);
    WixenPaneRect rects[4];
    wixen_panes_layout(&pt, rects, 4);
    /* Top pane: y=0, height=0.5 */
    ASSERT(APPROX_EQ(0, rects[0].y));
    ASSERT(APPROX_EQ(0.5, rects[0].height));
    /* Bottom pane: y=0.5, height=0.5 */
    ASSERT(APPROX_EQ(0.5, rects[1].y));
    ASSERT(APPROX_EQ(0.5, rects[1].height));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_close(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId new_id = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT_EQ(2, (int)wixen_panes_count(&pt));
    wixen_panes_close(&pt, new_id);
    ASSERT_EQ(1, (int)wixen_panes_count(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_close_only_pane_fails(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    ASSERT_FALSE(wixen_panes_close(&pt, root));
    ASSERT_EQ(1, (int)wixen_panes_count(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_focus(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId new_id = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT_EQ(new_id, wixen_panes_active(&pt)); /* Split focuses new pane */
    wixen_panes_focus(&pt, root);
    ASSERT_EQ(root, wixen_panes_active(&pt));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_zoom(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId p2 = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    ASSERT_FALSE(wixen_panes_is_zoomed(&pt));
    wixen_panes_zoom(&pt, root);
    ASSERT(wixen_panes_is_zoomed(&pt));
    /* Layout returns only zoomed pane */
    WixenPaneRect rects[4];
    size_t count = wixen_panes_layout(&pt, rects, 4);
    ASSERT_EQ(1, (int)count);
    ASSERT_EQ(root, rects[0].pane_id);
    ASSERT(APPROX_EQ(1, rects[0].width));
    wixen_panes_unzoom(&pt);
    ASSERT_FALSE(wixen_panes_is_zoomed(&pt));
    count = wixen_panes_layout(&pt, rects, 4);
    ASSERT_EQ(2, (int)count);
    (void)p2;
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_adjust_ratio(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    /* Grow first pane */
    wixen_panes_adjust_ratio(&pt, root, 0.1f);
    WixenPaneRect rects[4];
    wixen_panes_layout(&pt, rects, 4);
    ASSERT(APPROX_EQ(0.6, rects[0].width));
    ASSERT(APPROX_EQ(0.4, rects[1].width));
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_adjust_ratio_clamps(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    /* Try to grow way past limit */
    wixen_panes_adjust_ratio(&pt, root, 0.9f);
    WixenPaneRect rects[4];
    wixen_panes_layout(&pt, rects, 4);
    ASSERT(rects[0].width <= 0.91f); /* Clamped at 0.9 */
    ASSERT(rects[1].width >= 0.09f);
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_nested_split(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId p2 = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    WixenPaneId p3 = wixen_panes_split(&pt, p2, WIXEN_SPLIT_VERTICAL);
    ASSERT_EQ(3, (int)wixen_panes_count(&pt));
    WixenPaneRect rects[4];
    size_t count = wixen_panes_layout(&pt, rects, 4);
    ASSERT_EQ(3, (int)count);
    (void)p3;
    wixen_panes_free(&pt);
    PASS();
}

TEST panes_close_updates_active(void) {
    WixenPaneTree pt;
    WixenPaneId root;
    wixen_panes_init(&pt, &root);
    WixenPaneId p2 = wixen_panes_split(&pt, root, WIXEN_SPLIT_HORIZONTAL);
    wixen_panes_focus(&pt, p2);
    wixen_panes_close(&pt, p2);
    /* Active should be the remaining pane */
    ASSERT(wixen_panes_active(&pt) != p2);
    ASSERT_EQ(1, (int)wixen_panes_count(&pt));
    wixen_panes_free(&pt);
    PASS();
}

SUITE(pane_lifecycle) {
    RUN_TEST(panes_init_single);
    RUN_TEST(panes_layout_single);
}

SUITE(pane_splitting) {
    RUN_TEST(panes_split_horizontal);
    RUN_TEST(panes_split_vertical);
    RUN_TEST(panes_nested_split);
}

SUITE(pane_closing) {
    RUN_TEST(panes_close);
    RUN_TEST(panes_close_only_pane_fails);
    RUN_TEST(panes_close_updates_active);
}

SUITE(pane_focus_zoom) {
    RUN_TEST(panes_focus);
    RUN_TEST(panes_zoom);
}

SUITE(pane_resize) {
    RUN_TEST(panes_adjust_ratio);
    RUN_TEST(panes_adjust_ratio_clamps);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(pane_lifecycle);
    RUN_SUITE(pane_splitting);
    RUN_SUITE(pane_closing);
    RUN_SUITE(pane_focus_zoom);
    RUN_SUITE(pane_resize);
    GREATEST_MAIN_END();
}
