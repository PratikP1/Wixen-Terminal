/* panes.h — Split pane binary tree */
#ifndef WIXEN_UI_PANES_H
#define WIXEN_UI_PANES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t WixenPaneId;

typedef enum {
    WIXEN_SPLIT_HORIZONTAL = 0,  /* Left | Right */
    WIXEN_SPLIT_VERTICAL,        /* Top / Bottom */
} WixenSplitDirection;

typedef struct {
    WixenPaneId pane_id;
    float x, y, width, height;  /* Normalized [0,1] */
} WixenPaneRect;

/* Opaque pane tree node */
typedef struct WixenPaneNode WixenPaneNode;

struct WixenPaneNode {
    bool is_leaf;
    union {
        /* Leaf */
        struct { WixenPaneId id; } leaf;
        /* Split */
        struct {
            WixenSplitDirection direction;
            float ratio;           /* First child share [0.0, 1.0] */
            WixenPaneNode *first;
            WixenPaneNode *second;
        } split;
    };
};

typedef struct {
    WixenPaneNode *root;
    WixenPaneId active;
    WixenPaneId zoomed;        /* 0 = not zoomed */
    uint64_t next_id;
} WixenPaneTree;

/* Lifecycle */
void wixen_panes_init(WixenPaneTree *pt, WixenPaneId *out_root_pane);
void wixen_panes_free(WixenPaneTree *pt);

/* Operations */
WixenPaneId wixen_panes_split(WixenPaneTree *pt, WixenPaneId target,
                               WixenSplitDirection dir);
bool wixen_panes_close(WixenPaneTree *pt, WixenPaneId target);
void wixen_panes_focus(WixenPaneTree *pt, WixenPaneId target);

/* Layout — fills rects array (must have room for pane_count). Returns count. */
size_t wixen_panes_layout(const WixenPaneTree *pt, WixenPaneRect *rects, size_t cap);

/* Info */
size_t wixen_panes_count(const WixenPaneTree *pt);
WixenPaneId wixen_panes_active(const WixenPaneTree *pt);

/* Zoom */
void wixen_panes_zoom(WixenPaneTree *pt, WixenPaneId target);
void wixen_panes_unzoom(WixenPaneTree *pt);
bool wixen_panes_is_zoomed(const WixenPaneTree *pt);

/* Resize split ratio */
bool wixen_panes_adjust_ratio(WixenPaneTree *pt, WixenPaneId target, float delta);

#endif /* WIXEN_UI_PANES_H */
