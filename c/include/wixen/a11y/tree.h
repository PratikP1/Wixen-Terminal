/* tree.h — Virtual accessibility tree for terminal content */
#ifndef WIXEN_A11Y_TREE_H
#define WIXEN_A11Y_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t WixenNodeId;

typedef enum {
    WIXEN_ROLE_DOCUMENT = 0,
    WIXEN_ROLE_PROMPT,
    WIXEN_ROLE_COMMAND_INPUT,
    WIXEN_ROLE_OUTPUT_TEXT,
    WIXEN_ROLE_ERROR_TEXT,
    WIXEN_ROLE_PATH,
    WIXEN_ROLE_URL,
    WIXEN_ROLE_STATUS_LINE,
    WIXEN_ROLE_COMMAND_BLOCK,
} WixenSemanticRole;

typedef enum {
    WIXEN_NODE_DOCUMENT = 0,
    WIXEN_NODE_COMMAND_BLOCK,
    WIXEN_NODE_TEXT_REGION,
    WIXEN_NODE_HYPERLINK,
} WixenNodeType;

typedef struct WixenA11yNode WixenA11yNode;

struct WixenA11yNode {
    WixenNodeId id;
    WixenNodeType type;
    WixenSemanticRole role;
    char *text;
    char *name;              /* Accessible name */
    size_t start_row;
    size_t end_row;
    bool is_error;

    /* Children */
    WixenA11yNode *children;
    size_t child_count;
    size_t child_cap;
};

/* Command block view (for screen reader navigation) */
typedef struct {
    size_t prompt_start_row;
    size_t output_start_row;
    size_t output_end_row;
    int exit_code;
    bool completed;
    char *command_text;
} WixenA11yBlock;

typedef struct {
    WixenA11yNode root;
    WixenNodeId next_id;
    WixenA11yBlock *blocks;
    size_t block_count;
    size_t block_cap;
} WixenAccessibilityTree;

typedef WixenAccessibilityTree WixenA11yTree;

void wixen_a11y_tree_init(WixenAccessibilityTree *tree);
void wixen_a11y_tree_free(WixenAccessibilityTree *tree);

WixenA11yNode *wixen_a11y_tree_add_child(WixenAccessibilityTree *tree,
                                          WixenA11yNode *parent,
                                          WixenNodeType type,
                                          WixenSemanticRole role,
                                          const char *name);

void wixen_a11y_tree_clear(WixenAccessibilityTree *tree);
size_t wixen_a11y_tree_node_count(const WixenAccessibilityTree *tree);

/* Navigation helpers for provider */
WixenA11yNode *wixen_a11y_tree_child_at(WixenA11yTree *tree, size_t index);
int wixen_a11y_tree_find_block_at_row(const WixenA11yTree *tree, size_t row);

#include "wixen/shell_integ/shell_integ.h"
void wixen_a11y_tree_rebuild(WixenA11yTree *tree,
                              const WixenCommandBlock *blocks,
                              size_t block_count);
size_t wixen_a11y_tree_block_count(const WixenA11yTree *tree);
const WixenA11yBlock *wixen_a11y_tree_get_block(const WixenA11yTree *tree, size_t index);

#endif /* WIXEN_A11Y_TREE_H */
