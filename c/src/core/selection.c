/* selection.c — Terminal text selection */
#include "wixen/core/selection.h"
#include <string.h>

void wixen_selection_init(WixenSelection *sel) {
    memset(sel, 0, sizeof(*sel));
    sel->active = false;
}

void wixen_selection_start(WixenSelection *sel, size_t col, size_t row,
                           WixenSelectionType type) {
    sel->anchor.col = col;
    sel->anchor.row = row;
    sel->end.col = col;
    sel->end.row = row;
    sel->type = type;
    sel->active = true;
}

void wixen_selection_update(WixenSelection *sel, size_t col, size_t row) {
    sel->end.col = col;
    sel->end.row = row;
}

void wixen_selection_clear(WixenSelection *sel) {
    sel->active = false;
}

static bool point_before(WixenGridPoint a, WixenGridPoint b) {
    return a.row < b.row || (a.row == b.row && a.col < b.col);
}

void wixen_selection_ordered(const WixenSelection *sel,
                             WixenGridPoint *out_start, WixenGridPoint *out_end) {
    if (point_before(sel->anchor, sel->end)) {
        *out_start = sel->anchor;
        *out_end = sel->end;
    } else {
        *out_start = sel->end;
        *out_end = sel->anchor;
    }
}

bool wixen_selection_contains(const WixenSelection *sel, size_t col, size_t row,
                              size_t grid_cols) {
    if (!sel->active) return false;

    WixenGridPoint start, end;
    wixen_selection_ordered(sel, &start, &end);

    if (sel->type == WIXEN_SEL_BLOCK) {
        size_t min_col = start.col < end.col ? start.col : end.col;
        size_t max_col = start.col > end.col ? start.col : end.col;
        return row >= start.row && row <= end.row
            && col >= min_col && col <= max_col;
    }

    if (sel->type == WIXEN_SEL_LINE) {
        return row >= start.row && row <= end.row;
    }

    /* Normal or Word: linear range */
    (void)grid_cols;
    if (row < start.row || row > end.row) return false;
    if (row == start.row && row == end.row) {
        return col >= start.col && col <= end.col;
    }
    if (row == start.row) return col >= start.col;
    if (row == end.row) return col <= end.col;
    return true; /* Rows between start and end */
}

size_t wixen_selection_row_count(const WixenSelection *sel) {
    if (!sel->active) return 0;
    WixenGridPoint start, end;
    wixen_selection_ordered(sel, &start, &end);
    return end.row - start.row + 1;
}
