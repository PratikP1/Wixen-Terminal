/* test_red_copilot_p2_provider_nav.c — RED tests for provider navigation
 *
 * P2 completion: The provider's Navigate() must return command block
 * children, not always NULL. This is what makes NVDA able to traverse
 * the terminal's semantic structure.
 *
 * We test the tree navigation helpers that the provider will call.
 * The actual COM Navigate() can't be unit tested without a real HWND,
 * but we can test the logic that determines what Navigate returns.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/tree.h"
#include "wixen/shell_integ/shell_integ.h"

static WixenCommandBlock make_block(size_t ps, size_t os, size_t oe, int ec) {
    WixenCommandBlock b;
    memset(&b, 0, sizeof(b));
    b.prompt.start = ps; b.prompt.end = ps;
    b.output.start = os; b.output.end = oe;
    b.exit_code = ec; b.has_exit_code = true;
    b.state = WIXEN_BLOCK_COMPLETED;
    return b;
}

/* P2 nav a: find_block_at_row returns correct block index */
TEST red_p2nav_find_block_at_row(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[3] = {
        make_block(0, 1, 3, 0),
        make_block(4, 5, 7, 0),
        make_block(8, 9, 12, 1),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 3);

    /* Row 2 is in block 0 (output rows 1-3) */
    int idx = wixen_a11y_tree_find_block_at_row(&tree, 2);
    ASSERT_EQ(0, idx);

    /* Row 6 is in block 1 (output rows 5-7) */
    idx = wixen_a11y_tree_find_block_at_row(&tree, 6);
    ASSERT_EQ(1, idx);

    /* Row 10 is in block 2 (output rows 9-12) */
    idx = wixen_a11y_tree_find_block_at_row(&tree, 10);
    ASSERT_EQ(2, idx);

    /* Row 20 is not in any block */
    idx = wixen_a11y_tree_find_block_at_row(&tree, 20);
    ASSERT_EQ(-1, idx);

    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2 nav b: child node at index */
TEST red_p2nav_child_at_index(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        make_block(0, 1, 3, 0),
        make_block(4, 5, 8, 1),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    WixenA11yNode *child0 = wixen_a11y_tree_child_at(&tree, 0);
    ASSERT(child0 != NULL);
    ASSERT_EQ(WIXEN_NODE_COMMAND_BLOCK, child0->type);
    ASSERT_EQ(0, (int)child0->start_row);

    WixenA11yNode *child1 = wixen_a11y_tree_child_at(&tree, 1);
    ASSERT(child1 != NULL);
    ASSERT_EQ(4, (int)child1->start_row);

    /* Out of bounds */
    WixenA11yNode *child2 = wixen_a11y_tree_child_at(&tree, 2);
    ASSERT(child2 == NULL);

    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2 nav c: child accessible name includes command text or exit code */
TEST red_p2nav_child_name(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { make_block(0, 1, 3, 0) };
    blocks[0].command_text = "dir"; /* Not heap-allocated for test */
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenA11yNode *child = wixen_a11y_tree_child_at(&tree, 0);
    ASSERT(child != NULL);
    ASSERT(child->name != NULL);
    /* Name should include the command text */
    ASSERT(strstr(child->name, "dir") != NULL);

    /* Clean up — don't free command_text since it's a literal */
    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2 nav d: error block name includes "error" or exit code */
TEST red_p2nav_error_block_name(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { make_block(0, 1, 3, 127) };
    blocks[0].command_text = "badcmd";
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenA11yNode *child = wixen_a11y_tree_child_at(&tree, 0);
    ASSERT(child != NULL);
    ASSERT(child->is_error);
    /* Name should indicate error */
    ASSERT(child->name != NULL);
    ASSERT(strstr(child->name, "127") != NULL || strstr(child->name, "error") != NULL);

    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2 nav e: rebuild clears old children */
TEST red_p2nav_rebuild_clears(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);

    WixenCommandBlock blocks1[3] = {
        make_block(0, 1, 2, 0),
        make_block(3, 4, 5, 0),
        make_block(6, 7, 8, 0),
    };
    wixen_a11y_tree_rebuild(&tree, blocks1, 3);
    ASSERT_EQ(3, (int)tree.root.child_count);

    /* Rebuild with fewer blocks */
    WixenCommandBlock blocks2[1] = { make_block(0, 1, 5, 0) };
    wixen_a11y_tree_rebuild(&tree, blocks2, 1);
    ASSERT_EQ(1, (int)tree.root.child_count);

    wixen_a11y_tree_free(&tree);
    PASS();
}

SUITE(red_copilot_p2_provider_nav) {
    RUN_TEST(red_p2nav_find_block_at_row);
    RUN_TEST(red_p2nav_child_at_index);
    RUN_TEST(red_p2nav_child_name);
    RUN_TEST(red_p2nav_error_block_name);
    RUN_TEST(red_p2nav_rebuild_clears);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_copilot_p2_provider_nav);
    GREATEST_MAIN_END();
}
