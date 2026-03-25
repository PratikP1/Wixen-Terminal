/* tree.c — Virtual accessibility tree */
#include "wixen/a11y/tree.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

static void free_node_contents(WixenA11yNode *node) {
    free(node->text);
    free(node->name);
    for (size_t i = 0; i < node->child_count; i++) {
        free_node_contents(&node->children[i]);
    }
    free(node->children);
    node->text = NULL;
    node->name = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_cap = 0;
}

static size_t count_recursive(const WixenA11yNode *node) {
    size_t count = 1;
    for (size_t i = 0; i < node->child_count; i++) {
        count += count_recursive(&node->children[i]);
    }
    return count;
}

void wixen_a11y_tree_init(WixenAccessibilityTree *tree) {
    memset(tree, 0, sizeof(*tree));
    tree->root.id = 0;
    tree->root.type = WIXEN_NODE_DOCUMENT;
    tree->root.role = WIXEN_ROLE_DOCUMENT;
    tree->root.name = dup_str("Terminal");
    tree->next_id = 1;
}

void wixen_a11y_tree_free(WixenAccessibilityTree *tree) {
    free_node_contents(&tree->root);
    for (size_t i = 0; i < tree->block_count; i++) {
        free(tree->blocks[i].command_text);
    }
    free(tree->blocks);
    memset(tree, 0, sizeof(*tree));
}

WixenA11yNode *wixen_a11y_tree_add_child(WixenAccessibilityTree *tree,
                                          WixenA11yNode *parent,
                                          WixenNodeType type,
                                          WixenSemanticRole role,
                                          const char *name) {
    if (!parent) parent = &tree->root;

    if (parent->child_count >= parent->child_cap) {
        size_t new_cap = parent->child_cap ? parent->child_cap * 2 : 8;
        WixenA11yNode *new_arr = realloc(parent->children,
                                          new_cap * sizeof(WixenA11yNode));
        if (!new_arr) return NULL;
        parent->children = new_arr;
        parent->child_cap = new_cap;
    }

    WixenA11yNode *child = &parent->children[parent->child_count++];
    memset(child, 0, sizeof(*child));
    child->id = tree->next_id++;
    child->type = type;
    child->role = role;
    child->name = dup_str(name);
    return child;
}

void wixen_a11y_tree_clear(WixenAccessibilityTree *tree) {
    for (size_t i = 0; i < tree->root.child_count; i++) {
        free_node_contents(&tree->root.children[i]);
    }
    tree->root.child_count = 0;
    tree->next_id = 1;
}

size_t wixen_a11y_tree_node_count(const WixenAccessibilityTree *tree) {
    return count_recursive(&tree->root);
}

/* --- Command block rebuild from shell_integ --- */

#include "wixen/shell_integ/shell_integ.h"

#ifdef _MSC_VER
#define strdup _strdup
#endif

static void free_a11y_blocks(WixenAccessibilityTree *tree) {
    for (size_t i = 0; i < tree->block_count; i++) {
        free(tree->blocks[i].command_text);
    }
    tree->block_count = 0;
}

void wixen_a11y_tree_rebuild(WixenA11yTree *tree,
                              const WixenCommandBlock *blocks,
                              size_t block_count) {
    /* Clear old blocks */
    free_a11y_blocks(tree);

    /* Ensure capacity */
    if (block_count > tree->block_cap) {
        size_t new_cap = block_count + 4;
        WixenA11yBlock *new_arr = realloc(tree->blocks, new_cap * sizeof(WixenA11yBlock));
        if (!new_arr) return;
        tree->blocks = new_arr;
        tree->block_cap = new_cap;
    }

    /* Populate from shell_integ blocks */
    for (size_t i = 0; i < block_count; i++) {
        WixenA11yBlock *ab = &tree->blocks[i];
        memset(ab, 0, sizeof(*ab));
        ab->prompt_start_row = blocks[i].prompt.start;
        ab->output_start_row = blocks[i].output.start;
        ab->output_end_row = blocks[i].output.end;
        ab->exit_code = blocks[i].exit_code;
        ab->completed = (blocks[i].state == WIXEN_BLOCK_COMPLETED);
        ab->command_text = blocks[i].command_text ? strdup(blocks[i].command_text) : NULL;
    }
    tree->block_count = block_count;

    /* Also rebuild the node tree for UIA fragment navigation */
    wixen_a11y_tree_clear(tree);
    for (size_t i = 0; i < block_count; i++) {
        char name[128];
        if (blocks[i].command_text) {
            snprintf(name, sizeof(name), "Command: %s", blocks[i].command_text);
        } else {
            snprintf(name, sizeof(name), "Command block %zu", i + 1);
        }
        WixenA11yNode *block_node = wixen_a11y_tree_add_child(
            tree, &tree->root, WIXEN_NODE_COMMAND_BLOCK, WIXEN_ROLE_COMMAND_BLOCK, name);
        if (block_node) {
            block_node->start_row = blocks[i].prompt.start;
            block_node->end_row = blocks[i].output.end;
            block_node->is_error = (blocks[i].has_exit_code && blocks[i].exit_code != 0);
        }
    }
}

size_t wixen_a11y_tree_block_count(const WixenA11yTree *tree) {
    return tree->block_count;
}

const WixenA11yBlock *wixen_a11y_tree_get_block(const WixenA11yTree *tree, size_t index) {
    if (index >= tree->block_count) return NULL;
    return &tree->blocks[index];
}
