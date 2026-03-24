/* panes.c — Split pane binary tree */
#include "wixen/ui/panes.h"
#include <stdlib.h>
#include <string.h>

/* --- Node helpers --- */

static WixenPaneNode *make_leaf(WixenPaneId id) {
    WixenPaneNode *n = calloc(1, sizeof(WixenPaneNode));
    if (!n) return NULL;
    n->is_leaf = true;
    n->leaf.id = id;
    return n;
}

static void free_node(WixenPaneNode *n) {
    if (!n) return;
    if (!n->is_leaf) {
        free_node(n->split.first);
        free_node(n->split.second);
    }
    free(n);
}

static size_t count_leaves(const WixenPaneNode *n) {
    if (!n) return 0;
    if (n->is_leaf) return 1;
    return count_leaves(n->split.first) + count_leaves(n->split.second);
}

static WixenPaneNode *find_leaf(WixenPaneNode *n, WixenPaneId id) {
    if (!n) return NULL;
    if (n->is_leaf) return n->leaf.id == id ? n : NULL;
    WixenPaneNode *found = find_leaf(n->split.first, id);
    if (found) return found;
    return find_leaf(n->split.second, id);
}

static WixenPaneNode *find_parent(WixenPaneNode *n, WixenPaneId id, int *which_child) {
    if (!n || n->is_leaf) return NULL;
    if (n->split.first && n->split.first->is_leaf && n->split.first->leaf.id == id) {
        *which_child = 0;
        return n;
    }
    if (n->split.second && n->split.second->is_leaf && n->split.second->leaf.id == id) {
        *which_child = 1;
        return n;
    }
    WixenPaneNode *found = find_parent(n->split.first, id, which_child);
    if (found) return found;
    return find_parent(n->split.second, id, which_child);
}

static void collect_ids(const WixenPaneNode *n, WixenPaneId *ids, size_t *idx) {
    if (!n) return;
    if (n->is_leaf) {
        ids[(*idx)++] = n->leaf.id;
        return;
    }
    collect_ids(n->split.first, ids, idx);
    collect_ids(n->split.second, ids, idx);
}

static void layout_recursive(const WixenPaneNode *n, float x, float y, float w, float h,
                              WixenPaneRect *rects, size_t *idx, size_t cap) {
    if (!n || *idx >= cap) return;
    if (n->is_leaf) {
        WixenPaneRect *r = &rects[*idx];
        r->pane_id = n->leaf.id;
        r->x = x; r->y = y;
        r->width = w; r->height = h;
        (*idx)++;
        return;
    }
    float ratio = n->split.ratio;
    if (n->split.direction == WIXEN_SPLIT_HORIZONTAL) {
        float w1 = w * ratio;
        layout_recursive(n->split.first, x, y, w1, h, rects, idx, cap);
        layout_recursive(n->split.second, x + w1, y, w - w1, h, rects, idx, cap);
    } else {
        float h1 = h * ratio;
        layout_recursive(n->split.first, x, y, w, h1, rects, idx, cap);
        layout_recursive(n->split.second, x, y + h1, w, h - h1, rects, idx, cap);
    }
}

/* --- Lifecycle --- */

void wixen_panes_init(WixenPaneTree *pt, WixenPaneId *out_root_pane) {
    memset(pt, 0, sizeof(*pt));
    pt->next_id = 1;
    WixenPaneId id = pt->next_id++;
    pt->root = make_leaf(id);
    pt->active = id;
    if (out_root_pane) *out_root_pane = id;
}

void wixen_panes_free(WixenPaneTree *pt) {
    free_node(pt->root);
    memset(pt, 0, sizeof(*pt));
}

/* --- Split --- */

WixenPaneId wixen_panes_split(WixenPaneTree *pt, WixenPaneId target,
                               WixenSplitDirection dir) {
    /* Find the target leaf */
    int which = -1;
    WixenPaneNode *parent = find_parent(pt->root, target, &which);

    WixenPaneId new_id = pt->next_id++;

    /* Create new split node replacing the target leaf */
    WixenPaneNode *new_leaf = make_leaf(new_id);
    if (!new_leaf) return 0;

    WixenPaneNode *target_leaf;

    if (parent) {
        /* Target is a child of a split */
        target_leaf = (which == 0) ? parent->split.first : parent->split.second;

        WixenPaneNode *split = calloc(1, sizeof(WixenPaneNode));
        if (!split) { free(new_leaf); return 0; }
        split->is_leaf = false;
        split->split.direction = dir;
        split->split.ratio = 0.5f;
        split->split.first = target_leaf;
        split->split.second = new_leaf;

        if (which == 0) parent->split.first = split;
        else parent->split.second = split;
    } else if (pt->root && pt->root->is_leaf && pt->root->leaf.id == target) {
        /* Target is the root leaf */
        target_leaf = pt->root;
        WixenPaneNode *split = calloc(1, sizeof(WixenPaneNode));
        if (!split) { free(new_leaf); return 0; }
        split->is_leaf = false;
        split->split.direction = dir;
        split->split.ratio = 0.5f;
        split->split.first = target_leaf;
        split->split.second = new_leaf;
        pt->root = split;
    } else {
        free(new_leaf);
        return 0;
    }

    pt->active = new_id;
    return new_id;
}

/* --- Close --- */

bool wixen_panes_close(WixenPaneTree *pt, WixenPaneId target) {
    if (!pt->root) return false;
    /* Can't close the only pane */
    if (pt->root->is_leaf) return false;

    int which = -1;
    WixenPaneNode *parent = find_parent(pt->root, target, &which);
    if (!parent) return false;

    /* Replace parent split with the sibling */
    WixenPaneNode *to_remove = (which == 0) ? parent->split.first : parent->split.second;
    WixenPaneNode *sibling = (which == 0) ? parent->split.second : parent->split.first;

    /* Copy sibling into parent's position */
    free_node(to_remove);

    /* Find grandparent to replace parent with sibling */
    int gp_which = -1;
    WixenPaneNode *grandparent = NULL;
    if (pt->root == parent) {
        /* Parent is root — sibling becomes new root */
        free(parent); /* Don't free children — sibling is preserved */
        pt->root = sibling;
    } else {
        /* Find grandparent */
        /* Walk tree to find parent's parent */
        /* Since we don't have parent pointers, re-walk */
        /* This is O(n) but pane trees are small */
        grandparent = find_parent(pt->root, parent->split.first ?
            (parent->split.first->is_leaf ? parent->split.first->leaf.id : 0) : 0, &gp_which);
        if (!grandparent) {
            /* Try other child */
            grandparent = find_parent(pt->root, parent->split.second ?
                (parent->split.second->is_leaf ? parent->split.second->leaf.id : 0) : 0, &gp_which);
        }
        /* Simplified: just replace parent contents with sibling */
        if (sibling->is_leaf) {
            parent->is_leaf = true;
            parent->leaf.id = sibling->leaf.id;
        } else {
            parent->split = sibling->split;
        }
        free(sibling); /* Shell only, children preserved */
    }

    /* Update active pane if it was the closed one */
    if (pt->active == target) {
        /* Find first leaf */
        WixenPaneNode *first = pt->root;
        while (first && !first->is_leaf) first = first->split.first;
        pt->active = first ? first->leaf.id : 0;
    }

    if (pt->zoomed == target) pt->zoomed = 0;

    return true;
}

/* --- Focus --- */

void wixen_panes_focus(WixenPaneTree *pt, WixenPaneId target) {
    if (find_leaf(pt->root, target)) {
        pt->active = target;
    }
}

/* --- Layout --- */

size_t wixen_panes_layout(const WixenPaneTree *pt, WixenPaneRect *rects, size_t cap) {
    if (!pt->root || cap == 0) return 0;

    if (pt->zoomed) {
        /* Only show zoomed pane */
        rects[0].pane_id = pt->zoomed;
        rects[0].x = 0; rects[0].y = 0;
        rects[0].width = 1; rects[0].height = 1;
        return 1;
    }

    size_t idx = 0;
    layout_recursive(pt->root, 0, 0, 1, 1, rects, &idx, cap);
    return idx;
}

/* --- Info --- */

size_t wixen_panes_count(const WixenPaneTree *pt) {
    return count_leaves(pt->root);
}

WixenPaneId wixen_panes_active(const WixenPaneTree *pt) {
    return pt->active;
}

/* --- Zoom --- */

void wixen_panes_zoom(WixenPaneTree *pt, WixenPaneId target) {
    if (find_leaf(pt->root, target)) {
        pt->zoomed = target;
    }
}

void wixen_panes_unzoom(WixenPaneTree *pt) {
    pt->zoomed = 0;
}

bool wixen_panes_is_zoomed(const WixenPaneTree *pt) {
    return pt->zoomed != 0;
}

/* --- Resize --- */

bool wixen_panes_adjust_ratio(WixenPaneTree *pt, WixenPaneId target, float delta) {
    int which = -1;
    WixenPaneNode *parent = find_parent(pt->root, target, &which);
    if (!parent) return false;

    float new_ratio = parent->split.ratio + (which == 0 ? delta : -delta);
    if (new_ratio < 0.1f) new_ratio = 0.1f;
    if (new_ratio > 0.9f) new_ratio = 0.9f;
    parent->split.ratio = new_ratio;
    return true;
}
