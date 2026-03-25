/* test_red_copilot_p2_tree_nav.c — RED tests for Copilot finding P2
 *
 * P2: Provider exposes flat root, not semantic structure.
 * The command-block tree is built but provider doesn't navigate it.
 * Need: navigation functions, runtime IDs, child enumeration.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/tree.h"
#include "wixen/shell_integ/shell_integ.h"

static WixenCommandBlock make_block(size_t prompt_start, size_t output_start,
                                     size_t output_end, int exit_code) {
    WixenCommandBlock b;
    memset(&b, 0, sizeof(b));
    b.prompt.start = prompt_start;
    b.prompt.end = prompt_start;
    b.output.start = output_start;
    b.output.end = output_end;
    b.exit_code = exit_code;
    b.has_exit_code = true;
    b.state = WIXEN_BLOCK_COMPLETED;
    return b;
}

/* P2a: Tree with blocks has child nodes */
TEST red_p2_tree_has_children(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        make_block(0, 1, 3, 0),
        make_block(4, 5, 8, 1),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);
    /* Root should have 2 children (one per block) */
    ASSERT_EQ(2, (int)tree.root.child_count);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2b: Children have unique IDs */
TEST red_p2_children_have_unique_ids(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        make_block(0, 1, 3, 0),
        make_block(4, 5, 8, 1),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);
    ASSERT(tree.root.child_count >= 2);
    ASSERT(tree.root.children[0].id != tree.root.children[1].id);
    ASSERT(tree.root.children[0].id != tree.root.id);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2c: Child nodes have correct roles */
TEST red_p2_children_have_roles(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { make_block(0, 1, 3, 0) };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);
    ASSERT(tree.root.child_count >= 1);
    ASSERT_EQ(WIXEN_NODE_COMMAND_BLOCK, tree.root.children[0].type);
    ASSERT_EQ(WIXEN_ROLE_COMMAND_BLOCK, tree.root.children[0].role);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2d: Navigate first child */
TEST red_p2_navigate_first_child(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        make_block(0, 1, 3, 0),
        make_block(4, 5, 8, 1),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);
    /* First child of root should be accessible */
    ASSERT(tree.root.child_count > 0);
    ASSERT(tree.root.children[0].id > 0);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2e: Child stores row range for hit testing */
TEST red_p2_child_has_row_range(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { make_block(0, 1, 5, 0) };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);
    ASSERT(tree.root.child_count >= 1);
    WixenA11yNode *child = &tree.root.children[0];
    ASSERT_EQ(0, (int)child->start_row);
    ASSERT_EQ(5, (int)child->end_row);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2f: Error block has is_error flag */
TEST red_p2_error_block_flagged(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { make_block(0, 1, 3, 1) }; /* exit_code=1 */
    wixen_a11y_tree_rebuild(&tree, blocks, 1);
    ASSERT(tree.root.child_count >= 1);
    /* Block with non-zero exit code should be flagged as error */
    ASSERT(tree.root.children[0].is_error);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2g: Block count in a11y tree matches */
TEST red_p2_block_count_matches(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[3] = {
        make_block(0, 1, 3, 0),
        make_block(4, 5, 7, 0),
        make_block(8, 9, 12, 2),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 3);
    ASSERT_EQ(3, (int)tree.block_count);
    ASSERT_EQ(3, (int)tree.root.child_count);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* P2h: Empty tree has no children */
TEST red_p2_empty_tree(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    wixen_a11y_tree_rebuild(&tree, NULL, 0);
    ASSERT_EQ(0, (int)tree.root.child_count);
    ASSERT_EQ(0, (int)tree.block_count);
    wixen_a11y_tree_free(&tree);
    PASS();
}

SUITE(red_copilot_p2_tree_nav) {
    RUN_TEST(red_p2_tree_has_children);
    RUN_TEST(red_p2_children_have_unique_ids);
    RUN_TEST(red_p2_children_have_roles);
    RUN_TEST(red_p2_navigate_first_child);
    RUN_TEST(red_p2_child_has_row_range);
    RUN_TEST(red_p2_error_block_flagged);
    RUN_TEST(red_p2_block_count_matches);
    RUN_TEST(red_p2_empty_tree);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_copilot_p2_tree_nav);
    GREATEST_MAIN_END();
}
