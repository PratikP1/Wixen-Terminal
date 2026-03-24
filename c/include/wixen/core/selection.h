/* selection.h — Terminal text selection */
#ifndef WIXEN_CORE_SELECTION_H
#define WIXEN_CORE_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WIXEN_SEL_NORMAL = 0,    /* Character-by-character */
    WIXEN_SEL_WORD,          /* Word selection (double-click) */
    WIXEN_SEL_LINE,          /* Line selection (triple-click) */
    WIXEN_SEL_BLOCK,         /* Rectangular block */
} WixenSelectionType;

typedef struct {
    size_t col;
    size_t row;
} WixenGridPoint;

typedef struct {
    WixenGridPoint anchor;   /* Where selection started */
    WixenGridPoint end;      /* Moving end */
    WixenSelectionType type;
    bool active;
} WixenSelection;

void wixen_selection_init(WixenSelection *sel);
void wixen_selection_start(WixenSelection *sel, size_t col, size_t row,
                           WixenSelectionType type);
void wixen_selection_update(WixenSelection *sel, size_t col, size_t row);
void wixen_selection_clear(WixenSelection *sel);
bool wixen_selection_contains(const WixenSelection *sel, size_t col, size_t row,
                              size_t grid_cols);
/* Get ordered start/end (start <= end) */
void wixen_selection_ordered(const WixenSelection *sel,
                             WixenGridPoint *out_start, WixenGridPoint *out_end);
size_t wixen_selection_row_count(const WixenSelection *sel);

#endif /* WIXEN_CORE_SELECTION_H */
