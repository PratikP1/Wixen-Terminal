/* test_red_child_fragment.c — RED tests for command block child fragments
 *
 * Each command block in the tree needs a COM fragment provider that:
 * - Has a unique RuntimeId
 * - Reports its Name (command text)
 * - Reports its ControlType (Group)
 * - Navigates to parent (root) and siblings
 * - Reports bounding rectangle from row range
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/tree.h"
#include "wixen/a11y/child_fragment.h"
#include "wixen/shell_integ/shell_integ.h"

static WixenCommandBlock mb(size_t ps, size_t os, size_t oe, int ec, const char *cmd) {
    WixenCommandBlock b;
    memset(&b, 0, sizeof(b));
    b.prompt.start = ps; b.prompt.end = ps;
    b.output.start = os; b.output.end = oe;
    b.exit_code = ec; b.has_exit_code = true;
    b.state = WIXEN_BLOCK_COMPLETED;
    b.command_text = (char *)cmd;
    return b;
}

TEST red_child_create(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(0, 1, 3, 0, "dir") };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenChildFragment *cf = wixen_child_fragment_create(&tree, 0);
    ASSERT(cf != NULL);
    wixen_child_fragment_destroy(cf);
    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST red_child_name(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(0, 1, 3, 0, "git status") };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenChildFragment *cf = wixen_child_fragment_create(&tree, 0);
    ASSERT(cf != NULL);
    const char *name = wixen_child_fragment_name(cf);
    ASSERT(name != NULL);
    ASSERT(strstr(name, "git status") != NULL);
    wixen_child_fragment_destroy(cf);
    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST red_child_runtime_id(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 3, 0, "ls"),
        mb(4, 5, 8, 0, "pwd"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    WixenChildFragment *cf0 = wixen_child_fragment_create(&tree, 0);
    WixenChildFragment *cf1 = wixen_child_fragment_create(&tree, 1);
    ASSERT(cf0 != NULL);
    ASSERT(cf1 != NULL);

    int32_t id0 = wixen_child_fragment_runtime_id(cf0);
    int32_t id1 = wixen_child_fragment_runtime_id(cf1);
    ASSERT(id0 != id1);
    ASSERT(id0 > 0);

    wixen_child_fragment_destroy(cf0);
    wixen_child_fragment_destroy(cf1);
    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST red_child_is_error(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(0, 1, 3, 1, "bad") };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenChildFragment *cf = wixen_child_fragment_create(&tree, 0);
    ASSERT(cf != NULL);
    ASSERT(wixen_child_fragment_is_error(cf));
    wixen_child_fragment_destroy(cf);
    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST red_child_row_range(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(5, 6, 10, 0, "make") };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenChildFragment *cf = wixen_child_fragment_create(&tree, 0);
    ASSERT(cf != NULL);
    size_t start, end;
    wixen_child_fragment_row_range(cf, &start, &end);
    ASSERT_EQ(5, (int)start);
    ASSERT_EQ(10, (int)end);
    wixen_child_fragment_destroy(cf);
    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST red_child_sibling_nav(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[3] = {
        mb(0, 1, 2, 0, "a"),
        mb(3, 4, 5, 0, "b"),
        mb(6, 7, 8, 0, "c"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 3);

    /* Block 1 should have next=2 and prev=0 */
    ASSERT_EQ(2, wixen_child_fragment_next_sibling_index(&tree, 1));
    ASSERT_EQ(0, wixen_child_fragment_prev_sibling_index(&tree, 1));
    /* Block 0 has no prev, block 2 has no next */
    ASSERT_EQ(-1, wixen_child_fragment_prev_sibling_index(&tree, 0));
    ASSERT_EQ(-1, wixen_child_fragment_next_sibling_index(&tree, 2));

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    blocks[2].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

SUITE(red_child_fragment) {
    RUN_TEST(red_child_create);
    RUN_TEST(red_child_name);
    RUN_TEST(red_child_runtime_id);
    RUN_TEST(red_child_is_error);
    RUN_TEST(red_child_row_range);
    RUN_TEST(red_child_sibling_nav);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_child_fragment);
    GREATEST_MAIN_END();
}
