/* test_a11y_tree.c — Tests for accessibility tree */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/a11y/tree.h"

TEST tree_init(void) {
    WixenAccessibilityTree tree;
    wixen_a11y_tree_init(&tree);
    ASSERT_EQ(WIXEN_NODE_DOCUMENT, tree.root.type);
    ASSERT_STR_EQ("Terminal", tree.root.name);
    ASSERT_EQ(1, (int)wixen_a11y_tree_node_count(&tree));
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST tree_add_child(void) {
    WixenAccessibilityTree tree;
    wixen_a11y_tree_init(&tree);
    WixenA11yNode *child = wixen_a11y_tree_add_child(&tree, NULL,
        WIXEN_NODE_COMMAND_BLOCK, WIXEN_ROLE_COMMAND_BLOCK, "git status (exit 0)");
    ASSERT(child != NULL);
    ASSERT_EQ(WIXEN_NODE_COMMAND_BLOCK, child->type);
    ASSERT_STR_EQ("git status (exit 0)", child->name);
    ASSERT_EQ(2, (int)wixen_a11y_tree_node_count(&tree));
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST tree_nested_children(void) {
    WixenAccessibilityTree tree;
    wixen_a11y_tree_init(&tree);
    WixenA11yNode *block = wixen_a11y_tree_add_child(&tree, NULL,
        WIXEN_NODE_COMMAND_BLOCK, WIXEN_ROLE_COMMAND_BLOCK, "ls -la");
    WixenA11yNode *prompt = wixen_a11y_tree_add_child(&tree, block,
        WIXEN_NODE_TEXT_REGION, WIXEN_ROLE_PROMPT, "C:\\Users\\test>");
    WixenA11yNode *output = wixen_a11y_tree_add_child(&tree, block,
        WIXEN_NODE_TEXT_REGION, WIXEN_ROLE_OUTPUT_TEXT, "output lines");
    ASSERT(prompt != NULL);
    ASSERT(output != NULL);
    ASSERT_EQ(2, (int)block->child_count);
    ASSERT_EQ(4, (int)wixen_a11y_tree_node_count(&tree)); /* root + block + 2 children */
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST tree_clear(void) {
    WixenAccessibilityTree tree;
    wixen_a11y_tree_init(&tree);
    wixen_a11y_tree_add_child(&tree, NULL, WIXEN_NODE_COMMAND_BLOCK,
                               WIXEN_ROLE_COMMAND_BLOCK, "cmd1");
    wixen_a11y_tree_add_child(&tree, NULL, WIXEN_NODE_COMMAND_BLOCK,
                               WIXEN_ROLE_COMMAND_BLOCK, "cmd2");
    ASSERT_EQ(3, (int)wixen_a11y_tree_node_count(&tree));
    wixen_a11y_tree_clear(&tree);
    ASSERT_EQ(1, (int)wixen_a11y_tree_node_count(&tree)); /* Only root */
    ASSERT_EQ(0, (int)tree.root.child_count);
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST tree_ids_unique(void) {
    WixenAccessibilityTree tree;
    wixen_a11y_tree_init(&tree);
    WixenA11yNode *a = wixen_a11y_tree_add_child(&tree, NULL,
        WIXEN_NODE_TEXT_REGION, WIXEN_ROLE_OUTPUT_TEXT, "A");
    WixenA11yNode *b = wixen_a11y_tree_add_child(&tree, NULL,
        WIXEN_NODE_TEXT_REGION, WIXEN_ROLE_OUTPUT_TEXT, "B");
    ASSERT(a->id != b->id);
    ASSERT(a->id > 0);
    ASSERT(b->id > 0);
    wixen_a11y_tree_free(&tree);
    PASS();
}

TEST tree_error_node(void) {
    WixenAccessibilityTree tree;
    wixen_a11y_tree_init(&tree);
    WixenA11yNode *err = wixen_a11y_tree_add_child(&tree, NULL,
        WIXEN_NODE_TEXT_REGION, WIXEN_ROLE_ERROR_TEXT, "error: failed");
    err->is_error = true;
    ASSERT(err->is_error);
    ASSERT_EQ(WIXEN_ROLE_ERROR_TEXT, err->role);
    wixen_a11y_tree_free(&tree);
    PASS();
}

SUITE(a11y_tree_tests) {
    RUN_TEST(tree_init);
    RUN_TEST(tree_add_child);
    RUN_TEST(tree_nested_children);
    RUN_TEST(tree_clear);
    RUN_TEST(tree_ids_unique);
    RUN_TEST(tree_error_node);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(a11y_tree_tests);
    GREATEST_MAIN_END();
}
