/* test_red_com_child.c — RED tests for COM child fragment properties
 *
 * Tests the data layer that backs COM child fragment providers.
 * The COM vtable itself is Win32-only, but properties/navigation
 * must be correct before wiring into COM.
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
    b.exit_code = ec; b.has_exit_code = (ec >= 0);
    b.state = WIXEN_BLOCK_COMPLETED;
    b.command_text = (char *)cmd;
    return b;
}

/* Navigate FirstChild: root has children when tree has blocks */
TEST red_com_root_has_first_child(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 3, 0, "ls"),
        mb(4, 5, 8, 0, "pwd"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);
    ASSERT(tree.root.child_count == 2);

    /* First child should be index 0 */
    WixenChildFragment *first = wixen_child_fragment_create(&tree, 0);
    ASSERT(first != NULL);
    ASSERT(strstr(wixen_child_fragment_name(first), "ls") != NULL);
    wixen_child_fragment_destroy(first);

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Navigate LastChild: last block */
TEST red_com_root_has_last_child(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[3] = {
        mb(0, 1, 3, 0, "a"),
        mb(4, 5, 8, 0, "b"),
        mb(9, 10, 12, 0, "c"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 3);

    WixenChildFragment *last = wixen_child_fragment_create(&tree, tree.root.child_count - 1);
    ASSERT(last != NULL);
    ASSERT(strstr(wixen_child_fragment_name(last), "c") != NULL);
    wixen_child_fragment_destroy(last);

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    blocks[2].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Navigate NextSibling/PreviousSibling chain */
TEST red_com_sibling_chain(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[3] = {
        mb(0, 1, 3, 0, "first"),
        mb(4, 5, 8, 0, "second"),
        mb(9, 10, 12, 0, "third"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 3);

    /* Walk forward: 0→1→2→end */
    int idx = 0;
    ASSERT_EQ(1, wixen_child_fragment_next_sibling_index(&tree, (size_t)idx));
    ASSERT_EQ(2, wixen_child_fragment_next_sibling_index(&tree, 1));
    ASSERT_EQ(-1, wixen_child_fragment_next_sibling_index(&tree, 2));

    /* Walk backward: 2→1→0→end */
    ASSERT_EQ(1, wixen_child_fragment_prev_sibling_index(&tree, 2));
    ASSERT_EQ(0, wixen_child_fragment_prev_sibling_index(&tree, 1));
    ASSERT_EQ(-1, wixen_child_fragment_prev_sibling_index(&tree, 0));

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    blocks[2].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* Navigate Parent: child's parent is root */
TEST red_com_child_parent_is_root(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(0, 1, 3, 0, "cmd") };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenChildFragment *cf = wixen_child_fragment_create(&tree, 0);
    ASSERT(cf != NULL);
    /* The tree pointer IS the parent (root) */
    ASSERT(cf->tree == &tree);
    wixen_child_fragment_destroy(cf);

    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* ControlType: command blocks should report Group */
TEST red_com_child_control_type(void) {
    /* UIA_GroupControlTypeId = 50026 */
    #define UIA_GroupControlTypeId_val 50026
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[1] = { mb(0, 1, 3, 0, "dir") };
    wixen_a11y_tree_rebuild(&tree, blocks, 1);

    WixenChildFragment *cf = wixen_child_fragment_create(&tree, 0);
    int32_t ct = wixen_child_fragment_control_type(cf);
    ASSERT_EQ(UIA_GroupControlTypeId_val, ct);
    wixen_child_fragment_destroy(cf);

    blocks[0].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* RuntimeId uniqueness across rebuild */
TEST red_com_runtime_id_stable(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 3, 0, "x"),
        mb(4, 5, 8, 0, "y"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    WixenChildFragment *cf0 = wixen_child_fragment_create(&tree, 0);
    WixenChildFragment *cf1 = wixen_child_fragment_create(&tree, 1);
    int32_t id0 = wixen_child_fragment_runtime_id(cf0);
    int32_t id1 = wixen_child_fragment_runtime_id(cf1);
    ASSERT(id0 > 0);
    ASSERT(id1 > 0);
    ASSERT(id0 != id1);

    wixen_child_fragment_destroy(cf0);
    wixen_child_fragment_destroy(cf1);

    /* Rebuild with same blocks — IDs should be the same */
    wixen_a11y_tree_rebuild(&tree, blocks, 2);
    WixenChildFragment *cf0b = wixen_child_fragment_create(&tree, 0);
    int32_t id0b = wixen_child_fragment_runtime_id(cf0b);
    ASSERT_EQ(id0, id0b); /* Stable across rebuild */
    wixen_child_fragment_destroy(cf0b);

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* GetFocus with cursor row → child fragment */
TEST red_com_getfocus_returns_child(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 5, 0, "build"),
        mb(6, 7, 12, 1, "test"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    /* Cursor at row 8 → block 1 ("test") */
    int idx = wixen_a11y_tree_find_block_at_row(&tree, 8);
    ASSERT_EQ(1, idx);
    WixenChildFragment *focused = wixen_child_fragment_create(&tree, (size_t)idx);
    ASSERT(focused != NULL);
    ASSERT(strstr(wixen_child_fragment_name(focused), "test") != NULL);
    ASSERT(wixen_child_fragment_is_error(focused)); /* exit code 1 */
    wixen_child_fragment_destroy(focused);

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

/* ElementProviderFromPoint: y→row→block */
TEST red_com_hit_test_returns_child(void) {
    WixenA11yTree tree;
    wixen_a11y_tree_init(&tree);
    WixenCommandBlock blocks[2] = {
        mb(0, 1, 4, 0, "first"),
        mb(5, 6, 10, 0, "second"),
    };
    wixen_a11y_tree_rebuild(&tree, blocks, 2);

    /* y=112 with cell_height=16 → row 7 → block 1 */
    float cell_height = 16.0f;
    float y = 112.0f;
    size_t row = (size_t)(y / cell_height);
    ASSERT_EQ(7, (int)row);
    int idx = wixen_a11y_tree_find_block_at_row(&tree, row);
    ASSERT_EQ(1, idx);

    blocks[0].command_text = NULL;
    blocks[1].command_text = NULL;
    wixen_a11y_tree_free(&tree);
    PASS();
}

SUITE(red_com_child) {
    RUN_TEST(red_com_root_has_first_child);
    RUN_TEST(red_com_root_has_last_child);
    RUN_TEST(red_com_sibling_chain);
    RUN_TEST(red_com_child_parent_is_root);
    RUN_TEST(red_com_child_control_type);
    RUN_TEST(red_com_runtime_id_stable);
    RUN_TEST(red_com_getfocus_returns_child);
    RUN_TEST(red_com_hit_test_returns_child);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_com_child);
    GREATEST_MAIN_END();
}
