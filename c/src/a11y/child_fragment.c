/* child_fragment.c — Command block child fragment implementation */
#include "wixen/a11y/child_fragment.h"
#include "wixen/a11y/tree.h"
#include <stdlib.h>
#include <string.h>

WixenChildFragment *wixen_child_fragment_create(WixenA11yTree *tree, size_t block_index) {
    if (!tree || block_index >= tree->root.child_count) return NULL;
    WixenChildFragment *cf = calloc(1, sizeof(WixenChildFragment));
    if (!cf) return NULL;
    cf->tree = tree;
    cf->block_index = block_index;
    return cf;
}

void wixen_child_fragment_destroy(WixenChildFragment *cf) {
    free(cf);
}

const char *wixen_child_fragment_name(const WixenChildFragment *cf) {
    if (!cf || !cf->tree) return NULL;
    WixenA11yNode *node = wixen_a11y_tree_child_at(cf->tree, cf->block_index);
    return node ? node->name : NULL;
}

int32_t wixen_child_fragment_runtime_id(const WixenChildFragment *cf) {
    if (!cf || !cf->tree) return 0;
    WixenA11yNode *node = wixen_a11y_tree_child_at(cf->tree, cf->block_index);
    return node ? node->id : 0;
}

bool wixen_child_fragment_is_error(const WixenChildFragment *cf) {
    if (!cf || !cf->tree) return false;
    WixenA11yNode *node = wixen_a11y_tree_child_at(cf->tree, cf->block_index);
    return node ? node->is_error : false;
}

void wixen_child_fragment_row_range(const WixenChildFragment *cf,
                                     size_t *out_start, size_t *out_end) {
    if (!cf || !cf->tree) { *out_start = 0; *out_end = 0; return; }
    WixenA11yNode *node = wixen_a11y_tree_child_at(cf->tree, cf->block_index);
    if (node) {
        *out_start = node->start_row;
        *out_end = node->end_row;
    } else {
        *out_start = 0;
        *out_end = 0;
    }
}

int wixen_child_fragment_next_sibling_index(const WixenA11yTree *tree, size_t index) {
    if (!tree || index + 1 >= tree->root.child_count) return -1;
    return (int)(index + 1);
}

int wixen_child_fragment_prev_sibling_index(const WixenA11yTree *tree, size_t index) {
    if (!tree || index == 0) return -1;
    return (int)(index - 1);
}
