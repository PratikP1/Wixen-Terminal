/* grid.c — Terminal grid implementation */
#include "wixen/core/grid.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Row --- */

void wixen_row_init(WixenRow *row, size_t cols) {
    row->count = cols;
    row->wrapped = false;
    row->cells = calloc(cols, sizeof(WixenCell));
    if (!row->cells) {
        row->count = 0;
        return;
    }
    for (size_t i = 0; i < cols; i++) {
        wixen_cell_init(&row->cells[i]);
    }
}

void wixen_row_free(WixenRow *row) {
    if (row->cells) {
        for (size_t i = 0; i < row->count; i++) {
            wixen_cell_free(&row->cells[i]);
        }
        free(row->cells);
        row->cells = NULL;
    }
    row->count = 0;
}

void wixen_row_clone(WixenRow *dst, const WixenRow *src) {
    dst->count = src->count;
    dst->wrapped = src->wrapped;
    dst->cells = calloc(src->count, sizeof(WixenCell));
    if (!dst->cells) {
        dst->count = 0;
        return;
    }
    for (size_t i = 0; i < src->count; i++) {
        wixen_cell_clone(&dst->cells[i], &src->cells[i]);
    }
}

void wixen_row_clear(WixenRow *row) {
    for (size_t i = 0; i < row->count; i++) {
        wixen_cell_reset(&row->cells[i]);
    }
    row->wrapped = false;
}

size_t wixen_row_text(const WixenRow *row, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';

    /* Find last non-space cell */
    size_t last = 0;
    for (size_t i = 0; i < row->count; i++) {
        if (row->cells[i].content && strcmp(row->cells[i].content, " ") != 0
            && strcmp(row->cells[i].content, "") != 0) {
            last = i + 1;
        }
    }

    size_t written = 0;
    for (size_t i = 0; i < last && written < buf_size - 1; i++) {
        const char *c = row->cells[i].content;
        if (!c) c = " ";
        /* Skip wide-char continuation cells */
        if (row->cells[i].width == 0) continue;
        size_t len = strlen(c);
        if (written + len >= buf_size) break;
        memcpy(buf + written, c, len);
        written += len;
    }
    buf[written] = '\0';
    return written;
}

/* --- Grid --- */

void wixen_grid_init(WixenGrid *g, size_t cols, size_t rows) {
    g->cols = cols;
    g->num_rows = rows;
    g->rows = calloc(rows, sizeof(WixenRow));
    if (!g->rows) {
        g->num_rows = 0;
        return;
    }
    for (size_t i = 0; i < rows; i++) {
        wixen_row_init(&g->rows[i], cols);
    }
    wixen_cursor_init(&g->cursor);
    wixen_cell_attrs_default(&g->current_attrs);
}

void wixen_grid_free(WixenGrid *g) {
    if (g->rows) {
        for (size_t i = 0; i < g->num_rows; i++) {
            wixen_row_free(&g->rows[i]);
        }
        free(g->rows);
        g->rows = NULL;
    }
    g->num_rows = 0;
    g->cols = 0;
}

void wixen_grid_clear(WixenGrid *g) {
    for (size_t i = 0; i < g->num_rows; i++) {
        wixen_row_clear(&g->rows[i]);
    }
    wixen_cursor_init(&g->cursor);
}

WixenCell *wixen_grid_cell(WixenGrid *g, size_t col, size_t row) {
    if (row >= g->num_rows || col >= g->cols) return NULL;
    return &g->rows[row].cells[col];
}

const WixenCell *wixen_grid_cell_const(const WixenGrid *g, size_t col, size_t row) {
    if (row >= g->num_rows || col >= g->cols) return NULL;
    return &g->rows[row].cells[col];
}

void wixen_grid_write_char(WixenGrid *g, const char *utf8, uint8_t char_width) {
    if (!utf8 || g->num_rows == 0 || g->cols == 0) return;

    size_t col = g->cursor.col;
    size_t row = g->cursor.row;

    /* Auto-wrap if at right margin */
    if (col >= g->cols) {
        g->rows[row].wrapped = true;
        col = 0;
        row++;
        if (row >= g->num_rows) {
            wixen_grid_scroll_up(g, 1);
            row = g->num_rows - 1;
        }
    }

    /* Don't write wide char if only 1 col left */
    if (char_width == 2 && col + 1 >= g->cols && g->cols > 1) {
        /* Clear current cell, wrap to next line */
        wixen_cell_reset(&g->rows[row].cells[col]);
        g->rows[row].wrapped = true;
        col = 0;
        row++;
        if (row >= g->num_rows) {
            wixen_grid_scroll_up(g, 1);
            row = g->num_rows - 1;
        }
    }

    /* Write the cell */
    WixenCell *cell = &g->rows[row].cells[col];
    wixen_cell_set_content(cell, utf8);
    cell->attrs = g->current_attrs;
    cell->width = char_width;

    /* Write continuation cell for wide chars */
    if (char_width == 2 && col + 1 < g->cols) {
        WixenCell *cont = &g->rows[row].cells[col + 1];
        wixen_cell_set_content(cont, "");
        cont->attrs = g->current_attrs;
        cont->width = 0;
    }

    /* Advance cursor */
    g->cursor.col = col + char_width;
    g->cursor.row = row;
}

void wixen_grid_put_char(WixenGrid *g, size_t col, size_t row,
                         const char *utf8, uint8_t char_width) {
    if (row >= g->num_rows || col >= g->cols) return;
    WixenCell *cell = &g->rows[row].cells[col];
    wixen_cell_set_content(cell, utf8);
    cell->attrs = g->current_attrs;
    cell->width = char_width;
    if (char_width == 2 && col + 1 < g->cols) {
        WixenCell *cont = &g->rows[row].cells[col + 1];
        wixen_cell_set_content(cont, "");
        cont->attrs = g->current_attrs;
        cont->width = 0;
    }
}

/* --- Scrolling --- */

void wixen_grid_scroll_up(WixenGrid *g, size_t count) {
    wixen_grid_scroll_region_up(g, 0, g->num_rows, count);
}

void wixen_grid_scroll_down(WixenGrid *g, size_t count) {
    wixen_grid_scroll_region_down(g, 0, g->num_rows, count);
}

void wixen_grid_scroll_region_up(WixenGrid *g, size_t top, size_t bottom, size_t count) {
    if (top >= bottom || bottom > g->num_rows || count == 0) return;
    size_t region_size = bottom - top;
    if (count > region_size) count = region_size;

    /* Free rows being scrolled out */
    for (size_t i = 0; i < count; i++) {
        wixen_row_free(&g->rows[top + i]);
    }

    /* Move remaining rows up */
    size_t remaining = region_size - count;
    if (remaining > 0) {
        memmove(&g->rows[top], &g->rows[top + count], remaining * sizeof(WixenRow));
    }

    /* Init new blank rows at bottom of region */
    for (size_t i = 0; i < count; i++) {
        wixen_row_init(&g->rows[top + remaining + i], g->cols);
    }
}

void wixen_grid_scroll_region_down(WixenGrid *g, size_t top, size_t bottom, size_t count) {
    if (top >= bottom || bottom > g->num_rows || count == 0) return;
    size_t region_size = bottom - top;
    if (count > region_size) count = region_size;

    /* Free rows being scrolled out at bottom */
    for (size_t i = 0; i < count; i++) {
        wixen_row_free(&g->rows[bottom - 1 - i]);
    }

    /* Move remaining rows down */
    size_t remaining = region_size - count;
    if (remaining > 0) {
        memmove(&g->rows[top + count], &g->rows[top], remaining * sizeof(WixenRow));
    }

    /* Init new blank rows at top of region */
    for (size_t i = 0; i < count; i++) {
        wixen_row_init(&g->rows[top + i], g->cols);
    }
}

/* --- Line operations --- */

void wixen_grid_insert_lines(WixenGrid *g, size_t at_row, size_t count,
                             size_t region_top, size_t region_bottom) {
    if (at_row < region_top) at_row = region_top;
    if (at_row >= region_bottom) return;
    wixen_grid_scroll_region_down(g, at_row, region_bottom, count);
}

void wixen_grid_delete_lines(WixenGrid *g, size_t at_row, size_t count,
                             size_t region_top, size_t region_bottom) {
    if (at_row < region_top) at_row = region_top;
    if (at_row >= region_bottom) return;
    wixen_grid_scroll_region_up(g, at_row, region_bottom, count);
}

/* --- Cell operations --- */

void wixen_grid_insert_blank_cells(WixenGrid *g, size_t col, size_t row, size_t count) {
    if (row >= g->num_rows || col >= g->cols || count == 0) return;
    WixenRow *r = &g->rows[row];

    /* Free cells that will be pushed off the right edge */
    for (size_t i = g->cols - count; i < g->cols && i < g->cols; i++) {
        if (i >= col) wixen_cell_free(&r->cells[i]);
    }

    /* Shift cells right */
    size_t shift_count = g->cols - col - count;
    if (col + count < g->cols && shift_count > 0) {
        memmove(&r->cells[col + count], &r->cells[col], shift_count * sizeof(WixenCell));
    }

    /* Init blank cells at insertion point */
    for (size_t i = 0; i < count && col + i < g->cols; i++) {
        wixen_cell_init(&r->cells[col + i]);
    }
}

void wixen_grid_delete_cells(WixenGrid *g, size_t col, size_t row, size_t count) {
    if (row >= g->num_rows || col >= g->cols || count == 0) return;
    WixenRow *r = &g->rows[row];

    /* Free cells being deleted */
    for (size_t i = 0; i < count && col + i < g->cols; i++) {
        wixen_cell_free(&r->cells[col + i]);
    }

    /* Shift cells left */
    size_t remaining = g->cols - col - count;
    if (col + count < g->cols && remaining > 0) {
        memmove(&r->cells[col], &r->cells[col + count], remaining * sizeof(WixenCell));
    }

    /* Init blank cells at end */
    for (size_t i = g->cols - count; i < g->cols; i++) {
        if (i >= col) wixen_cell_init(&r->cells[i]);
    }
}

/* --- Erase --- */

void wixen_grid_erase_in_line(WixenGrid *g, size_t row, int mode) {
    if (row >= g->num_rows) return;
    WixenRow *r = &g->rows[row];
    size_t start, end;

    switch (mode) {
    case 0: /* cursor to end */
        start = g->cursor.col;
        end = g->cols;
        break;
    case 1: /* start to cursor */
        start = 0;
        end = g->cursor.col + 1;
        if (end > g->cols) end = g->cols;
        break;
    case 2: /* entire line */
        start = 0;
        end = g->cols;
        break;
    default:
        return;
    }

    for (size_t i = start; i < end; i++) {
        wixen_cell_reset(&r->cells[i]);
    }
}

void wixen_grid_erase_in_display(WixenGrid *g, int mode) {
    switch (mode) {
    case 0: /* cursor to end */
        /* Erase from cursor to end of current line */
        wixen_grid_erase_in_line(g, g->cursor.row, 0);
        /* Erase all lines below */
        for (size_t i = g->cursor.row + 1; i < g->num_rows; i++) {
            wixen_row_clear(&g->rows[i]);
        }
        break;
    case 1: /* start to cursor */
        /* Erase all lines above */
        for (size_t i = 0; i < g->cursor.row; i++) {
            wixen_row_clear(&g->rows[i]);
        }
        /* Erase from start to cursor on current line */
        wixen_grid_erase_in_line(g, g->cursor.row, 1);
        break;
    case 2: /* entire display */
        for (size_t i = 0; i < g->num_rows; i++) {
            wixen_row_clear(&g->rows[i]);
        }
        break;
    case 3: /* entire display + scrollback */
        for (size_t i = 0; i < g->num_rows; i++) {
            wixen_row_clear(&g->rows[i]);
        }
        /* Scrollback clearing handled at terminal level */
        break;
    default:
        break;
    }
}

/* --- Resize --- */

void wixen_grid_resize(WixenGrid *g, size_t new_cols, size_t new_rows) {
    if (new_cols == 0 || new_rows == 0) return;
    if (new_cols == g->cols && new_rows == g->num_rows) return;

    /* Resize columns for existing rows */
    size_t min_rows = g->num_rows < new_rows ? g->num_rows : new_rows;
    for (size_t i = 0; i < min_rows; i++) {
        WixenRow *r = &g->rows[i];
        if (new_cols > r->count) {
            /* Grow: realloc and init new cells */
            WixenCell *new_cells = realloc(r->cells, new_cols * sizeof(WixenCell));
            if (!new_cells) continue;
            r->cells = new_cells;
            for (size_t j = r->count; j < new_cols; j++) {
                wixen_cell_init(&r->cells[j]);
            }
        } else if (new_cols < r->count) {
            /* Shrink: free excess cells */
            for (size_t j = new_cols; j < r->count; j++) {
                wixen_cell_free(&r->cells[j]);
            }
            WixenCell *new_cells = realloc(r->cells, new_cols * sizeof(WixenCell));
            if (new_cells) r->cells = new_cells;
        }
        r->count = new_cols;
    }

    if (new_rows > g->num_rows) {
        /* Add new rows */
        WixenRow *new_row_arr = realloc(g->rows, new_rows * sizeof(WixenRow));
        if (!new_row_arr) return;
        g->rows = new_row_arr;
        for (size_t i = g->num_rows; i < new_rows; i++) {
            wixen_row_init(&g->rows[i], new_cols);
        }
    } else if (new_rows < g->num_rows) {
        /* Free excess rows */
        for (size_t i = new_rows; i < g->num_rows; i++) {
            wixen_row_free(&g->rows[i]);
        }
        WixenRow *new_row_arr = realloc(g->rows, new_rows * sizeof(WixenRow));
        if (new_row_arr) g->rows = new_row_arr;
    }

    g->cols = new_cols;
    g->num_rows = new_rows;
    wixen_cursor_clamp(&g->cursor, g->cols, g->num_rows);
}

void wixen_grid_resize_with_reflow(WixenGrid *g, size_t new_cols, size_t new_rows) {
    if (new_cols == 0 || new_rows == 0) return;
    if (new_cols == g->cols && new_rows == g->num_rows) return;

    /* Phase 1: Extract logical lines (join wrapped rows) */
    /* A logical line = sequence of rows where all but the last have wrapped=true */
    typedef struct { WixenCell *cells; size_t len; } LogicalLine;
    size_t ll_count = 0, ll_cap = g->num_rows;
    LogicalLine *lls = (LogicalLine *)calloc(ll_cap, sizeof(LogicalLine));
    if (!lls) { wixen_grid_resize(g, new_cols, new_rows); return; }

    for (size_t r = 0; r < g->num_rows; ) {
        /* Find end of logical line */
        size_t start = r;
        while (r < g->num_rows && g->rows[r].wrapped) r++;
        r++; /* Include the non-wrapped final row */
        if (r > g->num_rows) r = g->num_rows;

        /* Concatenate cells */
        size_t total_cells = 0;
        for (size_t i = start; i < r; i++) total_cells += g->rows[i].count;

        /* Find last non-space cell to trim */
        size_t trimmed = total_cells;
        size_t offset = 0;
        for (size_t i = start; i < r; i++) {
            for (size_t c = 0; c < g->rows[i].count; c++) {
                if (g->rows[i].cells[c].content &&
                    strcmp(g->rows[i].cells[c].content, " ") != 0 &&
                    g->rows[i].cells[c].content[0] != '\0') {
                    trimmed = offset + c + 1;
                }
            }
            offset += g->rows[i].count;
        }

        WixenCell *cells = (WixenCell *)calloc(trimmed > 0 ? trimmed : 1, sizeof(WixenCell));
        if (!cells) continue;
        offset = 0;
        for (size_t i = start; i < r && offset < trimmed; i++) {
            for (size_t c = 0; c < g->rows[i].count && offset < trimmed; c++) {
                wixen_cell_clone(&cells[offset], &g->rows[i].cells[c]);
                offset++;
            }
        }

        if (ll_count >= ll_cap) {
            ll_cap *= 2;
            LogicalLine *new_lls = (LogicalLine *)realloc(lls, ll_cap * sizeof(LogicalLine));
            if (!new_lls) { free(cells); break; }
            lls = new_lls;
        }
        lls[ll_count].cells = cells;
        lls[ll_count].len = trimmed;
        ll_count++;
    }

    /* Phase 2: Free old grid rows */
    for (size_t i = 0; i < g->num_rows; i++) wixen_row_free(&g->rows[i]);
    free(g->rows);

    /* Phase 3: Re-wrap logical lines at new_cols */
    g->cols = new_cols;
    g->num_rows = new_rows;
    g->rows = (WixenRow *)calloc(new_rows, sizeof(WixenRow));
    if (!g->rows) { g->num_rows = 0; goto cleanup; }
    for (size_t i = 0; i < new_rows; i++) wixen_row_init(&g->rows[i], new_cols);

    {
        size_t out_row = 0;
        for (size_t ll = 0; ll < ll_count && out_row < new_rows; ll++) {
            size_t src_pos = 0;
            while (src_pos < lls[ll].len && out_row < new_rows) {
                size_t chunk = lls[ll].len - src_pos;
                if (chunk > new_cols) chunk = new_cols;
                for (size_t c = 0; c < chunk; c++) {
                    wixen_cell_free(&g->rows[out_row].cells[c]);
                    wixen_cell_clone(&g->rows[out_row].cells[c], &lls[ll].cells[src_pos + c]);
                }
                src_pos += chunk;
                if (src_pos < lls[ll].len) {
                    g->rows[out_row].wrapped = true;
                }
                out_row++;
            }
        }
    }

    wixen_cursor_clamp(&g->cursor, g->cols, g->num_rows);

cleanup:
    for (size_t i = 0; i < ll_count; i++) {
        for (size_t c = 0; c < lls[i].len; c++) wixen_cell_free(&lls[i].cells[c]);
        free(lls[i].cells);
    }
    free(lls);
}

/* --- Text extraction --- */

size_t wixen_grid_visible_text(const WixenGrid *g, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return 0;
    buf[0] = '\0';
    size_t written = 0;
    char row_buf[4096];

    for (size_t i = 0; i < g->num_rows; i++) {
        size_t row_len = wixen_row_text(&g->rows[i], row_buf, sizeof(row_buf));
        if (i > 0 && written < buf_size - 1) {
            buf[written++] = '\n';
        }
        if (written + row_len >= buf_size) {
            size_t avail = buf_size - 1 - written;
            memcpy(buf + written, row_buf, avail);
            written += avail;
            break;
        }
        memcpy(buf + written, row_buf, row_len);
        written += row_len;
    }
    buf[written] = '\0';
    return written;
}

char *wixen_row_text_dynamic(const WixenRow *row) {
    if (!row) return NULL;
    size_t cap = row->count * 8 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t len = wixen_row_text(row, buf, cap);
    while (len > 0 && buf[len - 1] == ' ') len--;
    buf[len] = '\0';
    return buf;
}

char *wixen_grid_visible_text_dynamic(const WixenGrid *g) {
    if (!g || g->num_rows == 0) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t cap = g->num_rows * (g->cols * 4 + 2) + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t written = 0;
    for (size_t i = 0; i < g->num_rows; i++) {
        char *row_text = wixen_row_text_dynamic(&g->rows[i]);
        if (!row_text) continue;
        size_t rlen = strlen(row_text);
        if (i > 0) buf[written++] = '\n';
        memcpy(buf + written, row_text, rlen);
        written += rlen;
        free(row_text);
    }
    buf[written] = '\0';
    return buf;
}
