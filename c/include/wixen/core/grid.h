/* grid.h — Terminal grid (2D cell array with cursor) */
#ifndef WIXEN_CORE_GRID_H
#define WIXEN_CORE_GRID_H

#include "wixen/core/cell.h"
#include "wixen/core/cursor.h"
#include <stddef.h>
#include <stdbool.h>

/* --- Row --- */

typedef struct {
    WixenCell *cells;
    size_t count;       /* Always == parent grid's cols */
    bool wrapped;       /* Soft-wrap: cursor wrapped from right margin */
} WixenRow;

void wixen_row_init(WixenRow *row, size_t cols);
void wixen_row_free(WixenRow *row);
void wixen_row_clone(WixenRow *dst, const WixenRow *src);
void wixen_row_clear(WixenRow *row);
/* Extract visible text (trim trailing spaces) */
size_t wixen_row_text(const WixenRow *row, char *buf, size_t buf_size);

/* --- Grid --- */

typedef struct {
    WixenRow *rows;
    size_t cols;
    size_t num_rows;
    WixenCursor cursor;
    WixenCellAttributes current_attrs;
} WixenGrid;

/* Lifecycle */
void wixen_grid_init(WixenGrid *g, size_t cols, size_t rows);
void wixen_grid_free(WixenGrid *g);
void wixen_grid_clear(WixenGrid *g);
void wixen_grid_resize(WixenGrid *g, size_t new_cols, size_t new_rows);
void wixen_grid_resize_with_reflow(WixenGrid *g, size_t new_cols, size_t new_rows);

/* Cell access */
WixenCell *wixen_grid_cell(WixenGrid *g, size_t col, size_t row);
const WixenCell *wixen_grid_cell_const(const WixenGrid *g, size_t col, size_t row);

/* Writing */
void wixen_grid_write_char(WixenGrid *g, const char *utf8, uint8_t char_width);
void wixen_grid_put_char(WixenGrid *g, size_t col, size_t row,
                         const char *utf8, uint8_t char_width);

/* Scrolling */
void wixen_grid_scroll_up(WixenGrid *g, size_t count);
void wixen_grid_scroll_down(WixenGrid *g, size_t count);
void wixen_grid_scroll_region_up(WixenGrid *g, size_t top, size_t bottom, size_t count);
void wixen_grid_scroll_region_down(WixenGrid *g, size_t top, size_t bottom, size_t count);

/* Line operations */
void wixen_grid_insert_lines(WixenGrid *g, size_t at_row, size_t count,
                             size_t region_top, size_t region_bottom);
void wixen_grid_delete_lines(WixenGrid *g, size_t at_row, size_t count,
                             size_t region_top, size_t region_bottom);

/* Cell operations */
void wixen_grid_insert_blank_cells(WixenGrid *g, size_t col, size_t row, size_t count);
void wixen_grid_delete_cells(WixenGrid *g, size_t col, size_t row, size_t count);

/* Erase */
void wixen_grid_erase_in_line(WixenGrid *g, size_t row, int mode);
    /* mode: 0=cursor-to-end, 1=start-to-cursor, 2=entire-line */
void wixen_grid_erase_in_display(WixenGrid *g, int mode);
    /* mode: 0=cursor-to-end, 1=start-to-cursor, 2=entire, 3=scrollback */

/* Text extraction (full visible text, newline-separated) */
size_t wixen_grid_visible_text(const WixenGrid *g, char *buf, size_t buf_size);

/* Dynamic-allocation versions — no truncation. Caller must free(). */
char *wixen_row_text_dynamic(const WixenRow *row);
char *wixen_grid_visible_text_dynamic(const WixenGrid *g);

#endif /* WIXEN_CORE_GRID_H */
