/* test_red_a11y_tree_rebuild.c — RED tests for a11y tree rebuild from shell_integ blocks
 *
 * The a11y tree presents terminal content as a hierarchy of command blocks
 * (prompt → input → output) for screen readers. When shell_integ generation
 * changes, the tree must rebuild from the current blocks.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/tree.h"
#include "wixen/shell_integ/shell_integ.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

TEST red_tree_empty(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    ASSERT_EQ(0, (int)wixen_a11y_tree_block_count(&tree));
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST red_tree_rebuild_one_block(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);

    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0); /* Prompt at row 0 */
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1); /* Command starts row 1 */
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 3);  /* Done at row 3, exit 0 */

    size_t block_count = 0;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &block_count);
    ASSERT(block_count >= 1);

    wixen_a11y_tree_rebuild(&tree, blocks, block_count);
    ASSERT_EQ(1, (int)wixen_a11y_tree_block_count(&tree));

    /* Block should have prompt range, exit code */
    const WixenA11yBlock *ab = wixen_a11y_tree_get_block(&tree, 0);
    ASSERT(ab != NULL);
    ASSERT_EQ(0, (int)ab->exit_code);
    ASSERT(ab->completed);

    wixen_a11y_tree_free(&tree);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_tree_rebuild_multiple_blocks(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);

    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* Block 1 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 3);
    /* Block 2 */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 4);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 5);
    wixen_shell_integ_handle_osc133(&si, 'D', "1", 8);

    size_t block_count = 0;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &block_count);
    ASSERT(block_count >= 2);

    wixen_a11y_tree_rebuild(&tree, blocks, block_count);
    ASSERT_EQ(2, (int)wixen_a11y_tree_block_count(&tree));

    const WixenA11yBlock *b0 = wixen_a11y_tree_get_block(&tree, 0);
    const WixenA11yBlock *b1 = wixen_a11y_tree_get_block(&tree, 1);
    ASSERT(b0 != NULL && b1 != NULL);
    ASSERT_EQ(0, b0->exit_code);
    ASSERT_EQ(1, b1->exit_code);

    wixen_a11y_tree_free(&tree);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_tree_rebuild_replaces_old(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);

    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 1);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 2);

    size_t bc1 = 0;
    const WixenCommandBlock *b1 = wixen_shell_integ_blocks(&si, &bc1);
    wixen_a11y_tree_rebuild(&tree, b1, bc1);
    ASSERT_EQ(1, (int)wixen_a11y_tree_block_count(&tree));

    /* Add another block and rebuild */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 3);
    wixen_shell_integ_handle_osc133(&si, 'C', NULL, 4);
    wixen_shell_integ_handle_osc133(&si, 'D', "0", 5);

    size_t bc2 = 0;
    const WixenCommandBlock *b2 = wixen_shell_integ_blocks(&si, &bc2);
    wixen_a11y_tree_rebuild(&tree, b2, bc2);
    /* Should have 2 blocks now, not 3 (old tree replaced) */
    ASSERT_EQ(2, (int)wixen_a11y_tree_block_count(&tree));

    wixen_a11y_tree_free(&tree);
    wixen_shell_integ_free(&si);
    PASS();
}

TEST red_tree_incomplete_block(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);

    WixenShellIntegration si;
    wixen_shell_integ_init(&si);
    /* Prompt started but command not yet completed */
    wixen_shell_integ_handle_osc133(&si, 'A', NULL, 0);

    size_t bc = 0;
    const WixenCommandBlock *blocks = wixen_shell_integ_blocks(&si, &bc);
    wixen_a11y_tree_rebuild(&tree, blocks, bc);
    ASSERT_EQ(1, (int)wixen_a11y_tree_block_count(&tree));

    const WixenA11yBlock *ab = wixen_a11y_tree_get_block(&tree, 0);
    ASSERT(ab != NULL);
    ASSERT_FALSE(ab->completed);

    wixen_a11y_tree_free(&tree);
    wixen_shell_integ_free(&si);
    PASS();
}

SUITE(red_a11y_tree_rebuild) {
    RUN_TEST(red_tree_empty);
    RUN_TEST(red_tree_rebuild_one_block);
    RUN_TEST(red_tree_rebuild_multiple_blocks);
    RUN_TEST(red_tree_rebuild_replaces_old);
    RUN_TEST(red_tree_incomplete_block);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_a11y_tree_rebuild);
    GREATEST_MAIN_END();
}
