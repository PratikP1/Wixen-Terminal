/* test_red_smart_focus.c — RED tests for cursor-aware GetFocus and hit testing
 *
 * GetFocus should return the command block containing the cursor.
 * ElementProviderFromPoint should return the block at screen coordinates.
 * Both use wixen_a11y_tree_find_block_at_row() internally.
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "greatest.h"
#include "wixen/a11y/tree.h"
#include "wixen/a11y/child_fragment.h"
#include "wixen/shell_integ/shell_integ.h"

static WixenCommandBlock mb(size_t ps, size_t os, size_t oe, int ec) {
    WixenCommandBlock b;
    memset(&b, 0, sizeof(b));
    b.prompt.start = ps; b.prompt.end = ps;
    b.output.start = os; b.output.end = oe;
    b.exit_code = ec; b.has_exit_code = true;
    b.state = WIXEN_BLOCK_COMPLETED;
    return b;
}

/* Focus: cursor in block 1 returns block 1 */
TEST red_focus_cursor_in_block(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[3] = {
        mb(0, 1, 3, 0),
        mb(4, 5, 8, 0),
        mb(9, 10, 14, 0),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 3);

    /* Cursor at row 6 → block 1 */
    int idx = wixen_a11y_tree_find_block_at_row(&tree, 6);
    ASSERT_EQ(1, idx);
    WixenChildFragment *cf = wixen_child_fragment_create(&tree, (size_t)idx);
    ASSERT(cf != NULL);
    size_t s, e;
    wixen_child_fragment_row_range(cf, &s, &e);
    ASSERT_EQ(4, (int)s);
    ASSERT_EQ(8, (int)e);
    wixen_child_fragment_destroy(cf);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Focus: cursor between blocks returns -1 (root focus) */
TEST red_focus_cursor_between_blocks(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 3, 0),
        mb(8, 9, 12, 0), /* Gap: rows 4-7 not in any block */
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    int idx = wixen_a11y_tree_find_block_at_row(&tree, 5);
    ASSERT_EQ(-1, idx); /* Not in any block */
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Hit test: row from y coordinate */
TEST red_hit_test_row_from_y(void) {
    /* Given cell_height=16, y=80 → row 5 */
    float cell_height = 16.0f;
    float y = 80.0f;
    size_t row = (size_t)(y / cell_height);
    ASSERT_EQ(5, (int)row);
    PASS();
}

/* Hit test: block at y coordinate */
TEST red_hit_test_block_at_y(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 3, 0),  /* rows 0-3 */
        mb(4, 5, 8, 0),  /* rows 4-8 */
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    /* y=80 with cell_height=16 → row 5 → block 1 */
    size_t row = (size_t)(80.0f / 16.0f);
    int idx = wixen_a11y_tree_find_block_at_row(&tree, row);
    ASSERT_EQ(1, idx);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Focus: empty tree returns -1 */
TEST red_focus_empty_tree(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    wixen_a11y_tree_rebuild(&tree, NULL, 0);
    int idx = wixen_a11y_tree_find_block_at_row(&tree, 0);
    ASSERT_EQ(-1, idx);
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Focus: cursor at prompt row (included in block range) */
TEST red_focus_cursor_at_prompt(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(5, 6, 10, 0) };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    /* Row 5 is the prompt start → should be in block 0 */
    int idx = wixen_a11y_tree_find_block_at_row(&tree, 5);
    ASSERT_EQ(0, idx);
    wixen_a11y_tree_free(&tree);
    PASS();
}

SUITE(red_smart_focus) {
    RUN_TEST(red_focus_cursor_in_block);
    RUN_TEST(red_focus_cursor_between_blocks);
    RUN_TEST(red_hit_test_row_from_y);
    RUN_TEST(red_hit_test_block_at_y);
    RUN_TEST(red_focus_empty_tree);
    RUN_TEST(red_focus_cursor_at_prompt);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_smart_focus);
    GREATEST_MAIN_END();
}
