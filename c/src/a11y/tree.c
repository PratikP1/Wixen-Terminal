/* tree.c — Virtual accessibility tree */
#include "wixen/a11y/tree.h"
#include <stdlib.h>
#include <string.h>

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
