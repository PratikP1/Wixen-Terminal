/* terminal.c — Terminal emulator state machine */
#include "wixen/core/terminal.h"
#include "wixen/core/selection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- Helpers --- */

static size_t effective_top(const WixenTerminal *t) {
    return t->scroll_region.top;
}

static size_t effective_bottom(const WixenTerminal *t) {
    return t->scroll_region.bottom;
}

static size_t param_or(const WixenAction *a, size_t idx, uint16_t def) {
    if (a->type != WIXEN_ACTION_CSI_DISPATCH) return def;
    if (idx >= a->csi.param_count) return def;
    return a->csi.params[idx] == 0 ? def : a->csi.params[idx];
}

static bool has_private(const WixenAction *a) {
    if (a->type != WIXEN_ACTION_CSI_DISPATCH) return false;
    for (uint8_t i = 0; i < a->csi.intermediate_count; i++) {
        if (a->csi.intermediates[i] == '?') return true;
    }
    return false;
}

static void queue_response(WixenTerminal *t, const char *resp) {
    if (t->response_count >= t->response_cap) {
        size_t new_cap = t->response_cap ? t->response_cap * 2 : 8;
        char **new_arr = realloc(t->responses, new_cap * sizeof(char *));
        if (!new_arr) return;
        t->responses = new_arr;
        t->response_cap = new_cap;
    }
    size_t len = strlen(resp);
    char *copy = malloc(len + 1);
    if (!copy) return;
    memcpy(copy, resp, len + 1);
    t->responses[t->response_count++] = copy;
}

/* --- Tab stops --- */

static void init_tab_stops(WixenTabStops *ts, size_t cols) {
    ts->count = cols;
    ts->stops = calloc(cols, sizeof(bool));
    if (!ts->stops) { ts->count = 0; return; }
    /* Default: every 8 columns */
    for (size_t i = 0; i < cols; i += 8) {
        ts->stops[i] = true;
    }
}

static void free_tab_stops(WixenTabStops *ts) {
    free(ts->stops);
    ts->stops = NULL;
    ts->count = 0;
}

/* --- Lifecycle --- */

void wixen_terminal_init(WixenTerminal *t, size_t cols, size_t rows) {
    memset(t, 0, sizeof(*t));
    wixen_grid_init(&t->grid, cols, rows);
    wixen_modes_init(&t->modes);
    t->scroll_region.top = 0;
    t->scroll_region.bottom = rows;
    wixen_selection_init(&t->selection);
    wixen_hyperlinks_init(&t->hyperlinks);
    wixen_shell_integ_init(&t->shell_integ);
    init_tab_stops(&t->tab_stops, cols);
    t->dirty = true;
}

void wixen_terminal_free(WixenTerminal *t) {
    wixen_grid_free(&t->grid);
    if (t->alt_grid) {
        wixen_grid_free(t->alt_grid);
        free(t->alt_grid);
        t->alt_grid = NULL;
    }
    free(t->saved_cursor);
    free(t->title);
    wixen_hyperlinks_free(&t->hyperlinks);
    wixen_shell_integ_free(&t->shell_integ);
    free_tab_stops(&t->tab_stops);
    for (size_t i = 0; i < t->response_count; i++) {
        free(t->responses[i]);
    }
    free(t->responses);
}

void wixen_terminal_reset(WixenTerminal *t) {
    size_t cols = t->grid.cols;
    size_t rows = t->grid.num_rows;
    wixen_terminal_free(t);
    wixen_terminal_init(t, cols, rows);
}

void wixen_terminal_resize(WixenTerminal *t, size_t new_cols, size_t new_rows) {
    if (new_cols == 0 || new_rows == 0) return;
    wixen_grid_resize(&t->grid, new_cols, new_rows);
    if (t->alt_grid) {
        wixen_grid_resize(t->alt_grid, new_cols, new_rows);
    }
    t->scroll_region.top = 0;
    t->scroll_region.bottom = new_rows;
    /* Resize tab stops */
    free_tab_stops(&t->tab_stops);
    init_tab_stops(&t->tab_stops, new_cols);
    t->dirty = true;
}

void wixen_terminal_scroll_viewport(WixenTerminal *t, int32_t delta) {
    t->scroll_offset += delta;
    if (t->scroll_offset < 0) t->scroll_offset = 0;
    /* Clamp to scrollback length (can't scroll past oldest content) */
    /* For now, just ensure non-negative */
    t->dirty = true;
}

/* --- Cursor movement --- */

void wixen_terminal_move_cursor_up(WixenTerminal *t, size_t count) {
    size_t top = t->modes.origin_mode ? effective_top(t) : 0;
    if (t->grid.cursor.row < top + count)
        t->grid.cursor.row = top;
    else
        t->grid.cursor.row -= count;
    t->pending_wrap = false;
}

void wixen_terminal_move_cursor_down(WixenTerminal *t, size_t count) {
    size_t bottom = t->modes.origin_mode ? effective_bottom(t) : t->grid.num_rows;
    size_t max_row = bottom > 0 ? bottom - 1 : 0;
    if (t->grid.cursor.row + count > max_row)
        t->grid.cursor.row = max_row;
    else
        t->grid.cursor.row += count;
    t->pending_wrap = false;
}

void wixen_terminal_move_cursor_forward(WixenTerminal *t, size_t count) {
    size_t max_col = t->grid.cols > 0 ? t->grid.cols - 1 : 0;
    if (t->grid.cursor.col + count > max_col)
        t->grid.cursor.col = max_col;
    else
        t->grid.cursor.col += count;
    t->pending_wrap = false;
}

void wixen_terminal_move_cursor_backward(WixenTerminal *t, size_t count) {
    if (t->grid.cursor.col < count)
        t->grid.cursor.col = 0;
    else
        t->grid.cursor.col -= count;
    t->pending_wrap = false;
}

void wixen_terminal_set_cursor_pos(WixenTerminal *t, size_t row, size_t col) {
    size_t max_row = t->grid.num_rows > 0 ? t->grid.num_rows - 1 : 0;
    size_t max_col = t->grid.cols > 0 ? t->grid.cols - 1 : 0;

    if (t->modes.origin_mode) {
        row += effective_top(t);
        size_t bot = effective_bottom(t) > 0 ? effective_bottom(t) - 1 : 0;
        if (row > bot) row = bot;
    } else {
        if (row > max_row) row = max_row;
    }
    if (col > max_col) col = max_col;

    t->grid.cursor.row = row;
    t->grid.cursor.col = col;
    t->pending_wrap = false;
}

void wixen_terminal_cursor_home(WixenTerminal *t) {
    wixen_terminal_set_cursor_pos(t, 0, 0);
}

void wixen_terminal_carriage_return(WixenTerminal *t) {
    t->grid.cursor.col = 0;
    t->pending_wrap = false;
}

void wixen_terminal_linefeed(WixenTerminal *t) {
    size_t bottom = effective_bottom(t);
    if (t->grid.cursor.row + 1 >= bottom) {
        /* At bottom of scroll region — scroll up */
        wixen_grid_scroll_region_up(&t->grid, effective_top(t), bottom, 1);
    } else {
        t->grid.cursor.row++;
    }
    if (t->modes.line_feed_new_line) {
        t->grid.cursor.col = 0;
    }
    t->pending_wrap = false;
}

void wixen_terminal_index(WixenTerminal *t) {
    wixen_terminal_linefeed(t);
}

void wixen_terminal_reverse_index(WixenTerminal *t) {
    size_t top = effective_top(t);
    if (t->grid.cursor.row <= top) {
        wixen_grid_scroll_region_down(&t->grid, top, effective_bottom(t), 1);
    } else {
        t->grid.cursor.row--;
    }
    t->pending_wrap = false;
}

/* --- Tab stops --- */

void wixen_terminal_tab(WixenTerminal *t) {
    size_t col = t->grid.cursor.col + 1;
    while (col < t->grid.cols) {
        if (t->tab_stops.stops && col < t->tab_stops.count && t->tab_stops.stops[col]) {
            t->grid.cursor.col = col;
            t->pending_wrap = false;
            return;
        }
        col++;
    }
    /* No tab stop found — go to last column */
    t->grid.cursor.col = t->grid.cols > 0 ? t->grid.cols - 1 : 0;
    t->pending_wrap = false;
}

void wixen_terminal_set_tab_stop(WixenTerminal *t) {
    size_t col = t->grid.cursor.col;
    if (t->tab_stops.stops && col < t->tab_stops.count)
        t->tab_stops.stops[col] = true;
}

void wixen_terminal_clear_tab_stop(WixenTerminal *t, int mode) {
    if (!t->tab_stops.stops) return;
    switch (mode) {
    case 0: /* Clear at cursor */
        if (t->grid.cursor.col < t->tab_stops.count)
            t->tab_stops.stops[t->grid.cursor.col] = false;
        break;
    case 3: /* Clear all */
        memset(t->tab_stops.stops, 0, t->tab_stops.count * sizeof(bool));
        break;
    }
}

/* --- Erase --- */

void wixen_terminal_erase_in_display(WixenTerminal *t, int mode) {
    wixen_grid_erase_in_display(&t->grid, mode);
    t->dirty = true;
}

void wixen_terminal_erase_in_line(WixenTerminal *t, int mode) {
    wixen_grid_erase_in_line(&t->grid, t->grid.cursor.row, mode);
    t->dirty = true;
}

void wixen_terminal_erase_chars(WixenTerminal *t, size_t count) {
    size_t col = t->grid.cursor.col;
    size_t row = t->grid.cursor.row;
    for (size_t i = 0; i < count && col + i < t->grid.cols; i++) {
        WixenCell *cell = wixen_grid_cell(&t->grid, col + i, row);
        if (cell) wixen_cell_reset(cell);
    }
    t->dirty = true;
}

/* --- Line/cell operations --- */

void wixen_terminal_insert_lines(WixenTerminal *t, size_t count) {
    wixen_grid_insert_lines(&t->grid, t->grid.cursor.row, count,
                            effective_top(t), effective_bottom(t));
    t->dirty = true;
}

void wixen_terminal_delete_lines(WixenTerminal *t, size_t count) {
    wixen_grid_delete_lines(&t->grid, t->grid.cursor.row, count,
                            effective_top(t), effective_bottom(t));
    t->dirty = true;
}

void wixen_terminal_insert_blank_chars(WixenTerminal *t, size_t count) {
    wixen_grid_insert_blank_cells(&t->grid, t->grid.cursor.col,
                                  t->grid.cursor.row, count);
    t->dirty = true;
}

void wixen_terminal_delete_chars(WixenTerminal *t, size_t count) {
    wixen_grid_delete_cells(&t->grid, t->grid.cursor.col,
                            t->grid.cursor.row, count);
    t->dirty = true;
}

/* --- Scroll region --- */

void wixen_terminal_set_scroll_region(WixenTerminal *t, size_t top, size_t bottom) {
    if (top >= bottom || bottom > t->grid.num_rows) return;
    t->scroll_region.top = top;
    t->scroll_region.bottom = bottom;
    wixen_terminal_cursor_home(t);
}

/* --- Modes --- */

void wixen_terminal_set_mode(WixenTerminal *t, uint16_t mode, bool private_mode, bool set) {
    if (private_mode) {
        switch (mode) {
        case 1:  t->modes.cursor_keys_application = set; break;
        case 5:  t->modes.reverse_video = set; t->dirty = true; break;
        case 6:  t->modes.origin_mode = set;
                 if (set) wixen_terminal_cursor_home(t);
                 break;
        case 7:  t->modes.auto_wrap = set; break;
        case 25: t->modes.cursor_visible = set; break;
        case 47:
        case 1047:
            if (set) wixen_terminal_enter_alt_screen(t);
            else wixen_terminal_exit_alt_screen(t);
            break;
        case 1049:
            if (set) {
                wixen_terminal_save_cursor(t);
                wixen_terminal_enter_alt_screen(t);
            } else {
                wixen_terminal_exit_alt_screen(t);
                wixen_terminal_restore_cursor(t);
            }
            break;
        case 9:    t->modes.mouse_tracking = set ? WIXEN_MOUSE_X10 : WIXEN_MOUSE_NONE; break;
        case 1000: t->modes.mouse_tracking = set ? WIXEN_MOUSE_NORMAL : WIXEN_MOUSE_NONE; break;
        case 1002: t->modes.mouse_tracking = set ? WIXEN_MOUSE_BUTTON : WIXEN_MOUSE_NONE; break;
        case 1003: t->modes.mouse_tracking = set ? WIXEN_MOUSE_ANY : WIXEN_MOUSE_NONE; break;
        case 1006: t->modes.mouse_sgr = set; break;
        case 2004: t->modes.bracketed_paste = set; break;
        case 1004: t->modes.focus_reporting = set; break;
        case 2026: t->modes.synchronized_output = set; break;
        case 12:   t->grid.cursor.blinking = set; break;
        }
    } else {
        switch (mode) {
        case 4:  t->modes.insert_mode = set; break;
        case 20: t->modes.line_feed_new_line = set; break;
        }
    }
}

/* --- Alt screen --- */

void wixen_terminal_enter_alt_screen(WixenTerminal *t) {
    if (t->alt_grid) return; /* Already in alt screen */
    t->alt_grid = malloc(sizeof(WixenGrid));
    if (!t->alt_grid) return;
    /* Swap current grid to alt, create fresh main grid */
    *t->alt_grid = t->grid;
    wixen_grid_init(&t->grid, t->alt_grid->cols, t->alt_grid->num_rows);
    t->modes.alternate_screen = true;
    t->dirty = true;
}

void wixen_terminal_exit_alt_screen(WixenTerminal *t) {
    if (!t->alt_grid) return;
    wixen_grid_free(&t->grid);
    t->grid = *t->alt_grid;
    free(t->alt_grid);
    t->alt_grid = NULL;
    t->modes.alternate_screen = false;
    t->dirty = true;
}

/* --- Save/restore cursor --- */

void wixen_terminal_save_cursor(WixenTerminal *t) {
    if (!t->saved_cursor) {
        t->saved_cursor = malloc(sizeof(WixenSavedCursor));
        if (!t->saved_cursor) return;
    }
    t->saved_cursor->col = t->grid.cursor.col;
    t->saved_cursor->row = t->grid.cursor.row;
    t->saved_cursor->attrs = t->grid.current_attrs;
    t->saved_cursor->origin_mode = t->modes.origin_mode;
}

void wixen_terminal_restore_cursor(WixenTerminal *t) {
    if (!t->saved_cursor) return;
    t->grid.cursor.col = t->saved_cursor->col;
    t->grid.cursor.row = t->saved_cursor->row;
    t->grid.current_attrs = t->saved_cursor->attrs;
    t->modes.origin_mode = t->saved_cursor->origin_mode;
    wixen_cursor_clamp(&t->grid.cursor, t->grid.cols, t->grid.num_rows);
    t->pending_wrap = false;
}

/* --- SGR --- */

void wixen_terminal_apply_sgr(WixenTerminal *t, const WixenAction *action) {
    if (action->type != WIXEN_ACTION_CSI_DISPATCH) return;
    WixenCellAttributes *a = &t->grid.current_attrs;

    if (action->csi.param_count == 0) {
        /* SGR 0 — reset */
        wixen_cell_attrs_default(a);
        return;
    }

    for (uint8_t i = 0; i < action->csi.param_count; i++) {
        uint16_t p = action->csi.params[i];
        switch (p) {
        case 0: wixen_cell_attrs_default(a); break;
        case 1: a->bold = true; break;
        case 2: a->dim = true; break;
        case 3: a->italic = true; break;
        case 4:
            a->underline = WIXEN_UNDERLINE_SINGLE;
            /* Check for subparams (4:0, 4:1, 4:2, 4:3, etc.) */
            if (action->csi.subparam_counts[i] > 0) {
                switch (action->csi.subparams[i][0]) {
                case 0: a->underline = WIXEN_UNDERLINE_NONE; break;
                case 1: a->underline = WIXEN_UNDERLINE_SINGLE; break;
                case 2: a->underline = WIXEN_UNDERLINE_DOUBLE; break;
                case 3: a->underline = WIXEN_UNDERLINE_CURLY; break;
                case 4: a->underline = WIXEN_UNDERLINE_DOTTED; break;
                case 5: a->underline = WIXEN_UNDERLINE_DASHED; break;
                }
            }
            break;
        case 5: a->blink = true; break;
        case 7: a->inverse = true; break;
        case 8: a->hidden = true; break;
        case 9: a->strikethrough = true; break;
        case 21: a->underline = WIXEN_UNDERLINE_DOUBLE; break;
        case 22: a->bold = false; a->dim = false; break;
        case 23: a->italic = false; break;
        case 24: a->underline = WIXEN_UNDERLINE_NONE; break;
        case 25: a->blink = false; break;
        case 27: a->inverse = false; break;
        case 28: a->hidden = false; break;
        case 29: a->strikethrough = false; break;
        case 53: a->overline = true; break;
        case 55: a->overline = false; break;

        /* Foreground colors */
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            a->fg = wixen_color_indexed((uint8_t)(p - 30));
            break;
        case 38:
            if (i + 1 < action->csi.param_count) {
                if (action->csi.params[i + 1] == 5 && i + 2 < action->csi.param_count) {
                    a->fg = wixen_color_indexed((uint8_t)action->csi.params[i + 2]);
                    i += 2;
                } else if (action->csi.params[i + 1] == 2 && i + 4 < action->csi.param_count) {
                    a->fg = wixen_color_rgb(
                        (uint8_t)action->csi.params[i + 2],
                        (uint8_t)action->csi.params[i + 3],
                        (uint8_t)action->csi.params[i + 4]);
                    i += 4;
                }
            }
            break;
        case 39: a->fg = wixen_color_default(); break;

        /* Background colors */
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            a->bg = wixen_color_indexed((uint8_t)(p - 40));
            break;
        case 48:
            if (i + 1 < action->csi.param_count) {
                if (action->csi.params[i + 1] == 5 && i + 2 < action->csi.param_count) {
                    a->bg = wixen_color_indexed((uint8_t)action->csi.params[i + 2]);
                    i += 2;
                } else if (action->csi.params[i + 1] == 2 && i + 4 < action->csi.param_count) {
                    a->bg = wixen_color_rgb(
                        (uint8_t)action->csi.params[i + 2],
                        (uint8_t)action->csi.params[i + 3],
                        (uint8_t)action->csi.params[i + 4]);
                    i += 4;
                }
            }
            break;
        case 49: a->bg = wixen_color_default(); break;

        /* Bright foreground */
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            a->fg = wixen_color_indexed((uint8_t)(p - 90 + 8));
            break;

        /* Bright background */
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            a->bg = wixen_color_indexed((uint8_t)(p - 100 + 8));
            break;
        }
    }
}

/* --- Title --- */

void wixen_terminal_set_title(WixenTerminal *t, const char *title) {
    free(t->title);
    size_t len = strlen(title);
    t->title = malloc(len + 1);
    if (t->title) {
        memcpy(t->title, title, len + 1);
    }
    t->title_dirty = true;
}

/* --- Text extraction --- */

size_t wixen_terminal_visible_text_buf(const WixenTerminal *t, char *buf, size_t buf_size) {
    return wixen_grid_visible_text(&t->grid, buf, buf_size);
}

char *wixen_terminal_visible_text(const WixenTerminal *t) {
    /* Estimate: max 4 bytes per cell * cols * rows + newlines */
    size_t est = t->grid.cols * t->grid.num_rows * 4 + t->grid.num_rows + 1;
    char *buf = malloc(est);
    if (!buf) return NULL;
    size_t written = wixen_grid_visible_text(&t->grid, buf, est);
    buf[written] = '\0';
    return buf;
}

char *wixen_terminal_extract_row_text(const WixenTerminal *t, size_t row) {
    if (row >= t->grid.num_rows) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    const WixenRow *r = &t->grid.rows[row];
    /* Build text, then trim trailing spaces */
    size_t cap = t->grid.cols * 4 + 1;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;
    for (size_t c = 0; c < t->grid.cols; c++) {
        const char *content = r->cells[c].content;
        if (content[0]) {
            size_t len = strlen(content);
            if (pos + len < cap) {
                memcpy(buf + pos, content, len);
                pos += len;
            }
        } else {
            if (pos + 1 < cap) buf[pos++] = ' ';
        }
    }
    buf[pos] = '\0';
    /* Trim trailing spaces */
    while (pos > 0 && buf[pos - 1] == ' ') {
        pos--;
        buf[pos] = '\0';
    }
    return buf;
}

char *wixen_terminal_selected_text(const WixenTerminal *t, const WixenSelection *sel) {
    if (!sel || !sel->active) {
        char *empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    const WixenSelection *s = sel;
    WixenGridPoint start, end;
    wixen_selection_ordered(s, &start, &end);

    size_t cap = (end.row - start.row + 1) * (t->grid.cols * 4 + 2) + 1;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;

    for (size_t row = start.row; row <= end.row && row < t->grid.num_rows; row++) {
        size_t col_start = 0, col_end = t->grid.cols;
        if (s->type == WIXEN_SEL_NORMAL) {
            if (row == start.row) col_start = start.col;
            if (row == end.row) col_end = end.col + 1;
        } else if (s->type == WIXEN_SEL_BLOCK) {
            col_start = start.col;
            col_end = end.col + 1;
        }
        /* LINE selection: full row */
        if (col_end > t->grid.cols) col_end = t->grid.cols;

        const WixenRow *r = &t->grid.rows[row];
        size_t line_end = pos;
        for (size_t c = col_start; c < col_end; c++) {
            const char *content = r->cells[c].content;
            if (content[0]) {
                size_t len = strlen(content);
                if (pos + len < cap) { memcpy(buf + pos, content, len); pos += len; }
            } else {
                if (pos + 1 < cap) buf[pos++] = ' ';
            }
            line_end = pos;
        }
        /* Trim trailing spaces on this line */
        while (line_end > 0 && pos > 0 && buf[pos - 1] == ' ') pos--;
        /* Add newline between rows */
        if (row < end.row && pos + 1 < cap) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return buf;
}

/* --- Echo timeout detection --- */

#ifdef _WIN32
#include <windows.h>
static uint64_t get_ms(void) {
    return GetTickCount64();
}
#else
#include <time.h>
static uint64_t get_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

void wixen_terminal_on_char_sent(WixenTerminal *t, char ch) {
    t->last_char_sent_ms = get_ms();
    t->last_char_sent = ch;
    t->char_sent_pending = true;
    t->last_char_was_echoed = false;
}

bool wixen_terminal_check_echo_timeout(WixenTerminal *t) {
    if (!t->char_sent_pending) return false;
    if (t->last_char_was_echoed) {
        t->char_sent_pending = false;
        return false;
    }
    uint64_t now = get_ms();
    if (now - t->last_char_sent_ms > 1000) {
        t->char_sent_pending = false;
        return true; /* No echo after 1 second — password prompt */
    }
    return false;
}

/* --- Prompt jumping --- */

bool wixen_terminal_jump_to_previous_prompt(WixenTerminal *t) {
    /* Search backward from current cursor row for a shell_integ prompt marker */
    if (t->shell_integ.block_count == 0) return false;
    size_t cur = t->grid.cursor.row;
    for (int i = (int)t->shell_integ.block_count - 1; i >= 0; i--) {
        size_t prompt_row = t->shell_integ.blocks[i].prompt.start;
        if (prompt_row < cur) {
            t->grid.cursor.row = prompt_row;
            t->grid.cursor.col = 0;
            return true;
        }
    }
    return false;
}

bool wixen_terminal_jump_to_next_prompt(WixenTerminal *t) {
    if (t->shell_integ.block_count == 0) return false;
    size_t cur = t->grid.cursor.row;
    for (size_t i = 0; i < t->shell_integ.block_count; i++) {
        size_t prompt_row = t->shell_integ.blocks[i].prompt.start;
        if (prompt_row > cur) {
            t->grid.cursor.row = prompt_row;
            t->grid.cursor.col = 0;
            return true;
        }
    }
    return false;
}

/* --- Response queue --- */

const char *wixen_terminal_pop_response(WixenTerminal *t) {
    if (t->response_count == 0) return NULL;
    /* Pop front (shift) */
    char *resp = t->responses[0];
    memmove(&t->responses[0], &t->responses[1], (t->response_count - 1) * sizeof(char *));
    t->response_count--;
    return resp; /* Caller must free */
}

/* --- Print character (from ground state) --- */

/* DEC Special Graphics charset mapping (box drawing) */
static const uint32_t dec_special_graphics[128] = {
    [0x6a] = 0x2518, /* j → ┘ */  [0x6b] = 0x2510, /* k → ┐ */
    [0x6c] = 0x250C, /* l → ┌ */  [0x6d] = 0x2514, /* m → └ */
    [0x6e] = 0x253C, /* n → ┼ */  [0x71] = 0x2500, /* q → ─ */
    [0x74] = 0x251C, /* t → ├ */  [0x75] = 0x2524, /* u → ┤ */
    [0x76] = 0x2534, /* v → ┴ */  [0x77] = 0x252C, /* w → ┬ */
    [0x78] = 0x2502, /* x → │ */  [0x61] = 0x2592, /* a → ▒ */
    [0x60] = 0x25C6, /* ` → ◆ */  [0x66] = 0x00B0, /* f → ° */
    [0x67] = 0x00B1, /* g → ± */  [0x79] = 0x2264, /* y → ≤ */
    [0x7A] = 0x2265, /* z → ≥ */  [0x7B] = 0x03C0, /* { → π */
    [0x7E] = 0x00B7, /* ~ → · */
};

static void terminal_print(WixenTerminal *t, uint32_t codepoint) {
    t->last_printed_char = codepoint;

    /* Echo detection for password prompt */
    if (t->char_sent_pending && codepoint == (uint32_t)(unsigned char)t->last_char_sent) {
        t->last_char_was_echoed = true;
    }

    /* Apply charset translation (DEC Special Graphics for box drawing) */
    uint8_t active_set = t->charsets[t->active_charset];
    if (active_set == 1 && codepoint >= 0x60 && codepoint < 0x80) {
        uint32_t mapped = dec_special_graphics[codepoint];
        if (mapped) codepoint = mapped;
    }

    /* Encode codepoint to UTF-8 */
    char utf8[5] = {0};
    uint8_t char_width = 1;

    if (codepoint < 0x80) {
        utf8[0] = (char)codepoint;
    } else if (codepoint < 0x800) {
        utf8[0] = (char)(0xC0 | (codepoint >> 6));
        utf8[1] = (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        utf8[0] = (char)(0xE0 | (codepoint >> 12));
        utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (codepoint & 0x3F));
    } else {
        utf8[0] = (char)(0xF0 | (codepoint >> 18));
        utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (codepoint & 0x3F));
    }

    /* East Asian width detection — covers CJK, Hangul, fullwidth forms */
    if ((codepoint >= 0x1100 && codepoint <= 0x115F)
        || (codepoint >= 0x2E80 && codepoint <= 0xA4CF && codepoint != 0x303F)
        || (codepoint >= 0xAC00 && codepoint <= 0xD7A3)
        || (codepoint >= 0xF900 && codepoint <= 0xFAFF)
        || (codepoint >= 0xFE10 && codepoint <= 0xFE6F)
        || (codepoint >= 0xFF01 && codepoint <= 0xFF60)
        || (codepoint >= 0xFFE0 && codepoint <= 0xFFE6)
        || (codepoint >= 0x20000 && codepoint <= 0x2FFFD)
        || (codepoint >= 0x30000 && codepoint <= 0x3FFFD)) {
        char_width = 2;
    }

    /* Handle pending wrap */
    if (t->pending_wrap) {
        t->grid.rows[t->grid.cursor.row].wrapped = true;
        t->grid.cursor.col = 0;
        t->grid.cursor.row++;
        if (t->grid.cursor.row >= effective_bottom(t)) {
            wixen_grid_scroll_region_up(&t->grid, effective_top(t), effective_bottom(t), 1);
            t->grid.cursor.row = effective_bottom(t) - 1;
        }
        t->pending_wrap = false;
    }

    /* Insert mode: shift existing chars right */
    if (t->modes.insert_mode) {
        wixen_grid_insert_blank_cells(&t->grid, t->grid.cursor.col,
                                      t->grid.cursor.row, char_width);
    }

    /* Write the character */
    WixenCell *cell = wixen_grid_cell(&t->grid, t->grid.cursor.col, t->grid.cursor.row);
    if (!cell) return;
    wixen_cell_set_content(cell, utf8);
    cell->attrs = t->grid.current_attrs;
    cell->width = char_width;

    /* Continuation cell for wide chars */
    if (char_width == 2 && t->grid.cursor.col + 1 < t->grid.cols) {
        WixenCell *cont = wixen_grid_cell(&t->grid, t->grid.cursor.col + 1, t->grid.cursor.row);
        if (cont) {
            wixen_cell_set_content(cont, "");
            cont->attrs = t->grid.current_attrs;
            cont->width = 0;
        }
    }

    /* Advance cursor */
    t->grid.cursor.col += char_width;
    if (t->grid.cursor.col >= t->grid.cols) {
        if (t->modes.auto_wrap) {
            t->grid.cursor.col = t->grid.cols - 1;
            t->pending_wrap = true;
        } else {
            t->grid.cursor.col = t->grid.cols - 1;
        }
    }

    t->dirty = true;
}

/* --- Execute (C0 controls) --- */

static void terminal_execute(WixenTerminal *t, uint8_t byte) {
    switch (byte) {
    case 0x07: /* BEL */
        t->bell_pending = true;
        break;
    case 0x08: /* BS — backspace */
        if (t->grid.cursor.col > 0) t->grid.cursor.col--;
        t->pending_wrap = false;
        break;
    case 0x09: /* HT — tab */
        wixen_terminal_tab(t);
        break;
    case 0x0A: /* LF */
    case 0x0B: /* VT */
    case 0x0C: /* FF */
        wixen_terminal_linefeed(t);
        break;
    case 0x0D: /* CR */
        wixen_terminal_carriage_return(t);
        break;
    case 0x0E: /* SO — shift out (G1) */
        t->active_charset = 1;
        break;
    case 0x0F: /* SI — shift in (G0) */
        t->active_charset = 0;
        break;
    }
}

/* --- OSC dispatch --- */

static void terminal_osc(WixenTerminal *t, const uint8_t *data, size_t len) {
    if (len == 0) return;

    /* Find first ';' to split command number from payload */
    size_t sep = 0;
    while (sep < len && data[sep] != ';') sep++;

    /* Parse command number */
    int cmd = 0;
    for (size_t i = 0; i < sep; i++) {
        if (data[i] >= '0' && data[i] <= '9')
            cmd = cmd * 10 + (data[i] - '0');
    }

    const char *payload = sep + 1 < len ? (const char *)(data + sep + 1) : "";
    size_t payload_len = sep + 1 < len ? len - sep - 1 : 0;

    switch (cmd) {
    case 0: /* Set icon name + title */
    case 2: { /* Set title */
        char *title = malloc(payload_len + 1);
        if (title) {
            memcpy(title, payload, payload_len);
            title[payload_len] = '\0';
            wixen_terminal_set_title(t, title);
            free(title);
        }
        break;
    }
    case 7: { /* CWD — file://hostname/path */
        char *uri = malloc(payload_len + 1);
        if (uri) {
            memcpy(uri, payload, payload_len);
            uri[payload_len] = '\0';
            wixen_shell_integ_handle_osc7(&t->shell_integ, uri);
            free(uri);
        }
        break;
    }
    case 133: { /* Shell integration markers */
        /* Payload format: "A", "B", "C", or "D;exitcode" */
        if (payload_len >= 1) {
            char marker = payload[0];
            const char *params = payload_len > 2 ? payload + 2 : NULL;
            wixen_shell_integ_handle_osc133(
                &t->shell_integ, marker, params, t->grid.cursor.row);
        }
        break;
    }
    case 4: { /* Color palette change — OSC 4;index;spec */
        /* Parse index and color spec, update palette */
        /* For now, acknowledge but don't change palette */
        break;
    }
    case 8: { /* Hyperlinks — OSC 8;params;uri */
        /* Format: params (e.g., "id=xxx") ; uri */
        /* Empty uri = close link. Non-empty = open link. */
        const char *semi = memchr(payload, ';', payload_len);
        if (semi) {
            size_t params_len = (size_t)(semi - payload);
            const char *uri_start = semi + 1;
            size_t uri_len = payload_len - params_len - 1;
            if (uri_len > 0) {
                /* Open hyperlink — store in hyperlink table */
                char *uri = malloc(uri_len + 1);
                if (uri) {
                    memcpy(uri, uri_start, uri_len);
                    uri[uri_len] = '\0';
                    /* Extract id parameter if present */
                    const char *id = NULL;
                    char id_buf[128] = {0};
                    if (params_len > 3 && memcmp(payload, "id=", 3) == 0) {
                        size_t id_len = params_len - 3;
                        if (id_len < sizeof(id_buf)) {
                            memcpy(id_buf, payload + 3, id_len);
                            id = id_buf;
                        }
                    }
                    uint32_t link_id = wixen_hyperlinks_get_or_insert(&t->hyperlinks, uri, id);
                    t->current_hyperlink_id = link_id;
                    free(uri);
                }
            } else {
                /* Close hyperlink */
                if (t->current_hyperlink_id) {
                    wixen_hyperlinks_close(&t->hyperlinks);
                    t->current_hyperlink_id = 0;
                }
            }
        }
        break;
    }
    case 52: { /* Clipboard — OSC 52;selection;base64-data */
        /* selection = "c" (clipboard), "p" (primary), "s" (select) */
        /* For now, store the clipboard request for the host to handle */
        /* A "?" query means the app wants clipboard contents */
        break;
    }
    case 12: /* Cursor color query */
        if (payload_len > 0 && payload[0] == '?') {
            queue_response(t, "\x1b]12;rgb:ff/ff/ff\x1b\\");
        }
        break;
    case 10: /* Foreground color query — respond with default FG */
        if (payload_len > 0 && payload[0] == '?') {
            /* Respond: OSC 10;rgb:ff/ff/ff ST (white default) */
            queue_response(t, "\x1b]10;rgb:ff/ff/ff\x1b\\");
        }
        break;
    case 11: /* Background color query — respond with default BG */
        if (payload_len > 0 && payload[0] == '?') {
            queue_response(t, "\x1b]11;rgb:00/00/00\x1b\\");
        }
        break;
    }
}

/* --- CSI dispatch --- */

static void terminal_csi(WixenTerminal *t, const WixenAction *action) {
    char final = action->csi.final_byte;
    bool priv = has_private(action);

    switch (final) {
    case 'A': /* CUU — cursor up */
        wixen_terminal_move_cursor_up(t, param_or(action, 0, 1));
        break;
    case 'B': /* CUD — cursor down */
    case 'e': /* VPA relative — same as CUD */
        wixen_terminal_move_cursor_down(t, param_or(action, 0, 1));
        break;
    case 'C': /* CUF — cursor forward */
    case 'a': /* HPR — same as CUF */
        wixen_terminal_move_cursor_forward(t, param_or(action, 0, 1));
        break;
    case 'D': /* CUB — cursor backward */
        wixen_terminal_move_cursor_backward(t, param_or(action, 0, 1));
        break;
    case 'E': /* CNL — cursor next line */
        wixen_terminal_move_cursor_down(t, param_or(action, 0, 1));
        t->grid.cursor.col = 0;
        break;
    case 'F': /* CPL — cursor prev line */
        wixen_terminal_move_cursor_up(t, param_or(action, 0, 1));
        t->grid.cursor.col = 0;
        break;
    case 'G': /* CHA — cursor horizontal absolute */
    case '`': /* HPA */
        t->grid.cursor.col = param_or(action, 0, 1) - 1;
        if (t->grid.cursor.col >= t->grid.cols)
            t->grid.cursor.col = t->grid.cols > 0 ? t->grid.cols - 1 : 0;
        t->pending_wrap = false;
        break;
    case 'H': /* CUP — cursor position */
    case 'f': /* HVP */
        wixen_terminal_set_cursor_pos(t,
            param_or(action, 0, 1) - 1,
            param_or(action, 1, 1) - 1);
        break;
    case 'J': /* ED — erase in display */
        wixen_terminal_erase_in_display(t, (int)param_or(action, 0, 0));
        break;
    case 'K': /* EL — erase in line */
        wixen_terminal_erase_in_line(t, (int)param_or(action, 0, 0));
        break;
    case 'L': /* IL — insert lines */
        wixen_terminal_insert_lines(t, param_or(action, 0, 1));
        break;
    case 'M': /* DL — delete lines */
        wixen_terminal_delete_lines(t, param_or(action, 0, 1));
        break;
    case 'P': /* DCH — delete characters */
        wixen_terminal_delete_chars(t, param_or(action, 0, 1));
        break;
    case 'X': /* ECH — erase characters */
        wixen_terminal_erase_chars(t, param_or(action, 0, 1));
        break;
    case '@': /* ICH — insert blank characters */
        wixen_terminal_insert_blank_chars(t, param_or(action, 0, 1));
        break;
    case 'b': /* REP — repeat last printed character */
        if (t->last_printed_char) {
            size_t count = param_or(action, 0, 1);
            for (size_t i = 0; i < count; i++)
                terminal_print(t, t->last_printed_char);
        }
        break;
    case 'Z': /* CBT — cursor backward tabulation */
    {
        size_t count = param_or(action, 0, 1);
        for (size_t i = 0; i < count; i++) {
            if (t->grid.cursor.col == 0) break;
            t->grid.cursor.col--;
            while (t->grid.cursor.col > 0 &&
                   !(t->tab_stops.stops && t->tab_stops.stops[t->grid.cursor.col]))
                t->grid.cursor.col--;
        }
        t->pending_wrap = false;
        break;
    }
    case 'd': /* VPA — vertical position absolute */
        t->grid.cursor.row = param_or(action, 0, 1) - 1;
        if (t->grid.cursor.row >= t->grid.num_rows)
            t->grid.cursor.row = t->grid.num_rows > 0 ? t->grid.num_rows - 1 : 0;
        t->pending_wrap = false;
        break;
    case 'r': /* DECSTBM — set scroll region */
        if (!priv) {
            size_t top = param_or(action, 0, 1) - 1;
            size_t bot = param_or(action, 1, (uint16_t)t->grid.num_rows);
            wixen_terminal_set_scroll_region(t, top, bot);
        }
        break;
    case 'p': /* DECSTR — soft terminal reset (CSI ! p) */
        if (action->csi.intermediate_count > 0 && action->csi.intermediates[0] == '!') {
            wixen_modes_reset(&t->modes);
            t->modes.auto_wrap = true;
            t->modes.cursor_visible = true;
            t->scroll_region.top = 0;
            t->scroll_region.bottom = t->grid.num_rows;
            t->grid.cursor.row = 0;
            t->grid.cursor.col = 0;
            t->pending_wrap = false;
            t->dirty = true;
        }
        break;
    case 't': /* XTWINOPS — window operations */
        if (!priv) {
            uint16_t op = (uint16_t)param_or(action, 0, 0);
            switch (op) {
            case 8: { /* Report text area size in chars */
                char resp[32];
                snprintf(resp, sizeof(resp), "\x1b[8;%zu;%zut",
                    t->grid.num_rows, t->grid.cols);
                queue_response(t, resp);
                break;
            }
            case 14: { /* Report text area size in pixels (approximate) */
                char resp[32];
                snprintf(resp, sizeof(resp), "\x1b[4;%zu;%zut",
                    t->grid.num_rows * 16, t->grid.cols * 8);
                queue_response(t, resp);
                break;
            }
            case 18: { /* Report terminal size in chars */
                char resp[32];
                snprintf(resp, sizeof(resp), "\x1b[8;%zu;%zut",
                    t->grid.num_rows, t->grid.cols);
                queue_response(t, resp);
                break;
            }
            case 22: /* Push title (save) — ignored for now */
            case 23: /* Pop title (restore) — ignored for now */
                break;
            }
        }
        break;
    case 'm': /* SGR — select graphic rendition */
        wixen_terminal_apply_sgr(t, action);
        break;
    case 'h': /* SM/DECSET — set mode */
        for (uint8_t i = 0; i < action->csi.param_count; i++)
            wixen_terminal_set_mode(t, action->csi.params[i], priv, true);
        break;
    case 'l': /* RM/DECRST — reset mode */
        for (uint8_t i = 0; i < action->csi.param_count; i++)
            wixen_terminal_set_mode(t, action->csi.params[i], priv, false);
        break;
    case 's': /* SCOSC — save cursor (when not private) */
        if (!priv) wixen_terminal_save_cursor(t);
        break;
    case 'u': /* SCORC — restore cursor (when not private) */
        if (!priv) wixen_terminal_restore_cursor(t);
        break;
    case 'g': /* TBC — tab clear */
        wixen_terminal_clear_tab_stop(t, (int)param_or(action, 0, 0));
        break;
    case 'n': /* DSR — device status report */
        if (param_or(action, 0, 0) == 6) {
            /* CPR — cursor position report */
            char buf[32];
            snprintf(buf, sizeof(buf), "\x1b[%zu;%zuR",
                     t->grid.cursor.row + 1, t->grid.cursor.col + 1);
            queue_response(t, buf);
        } else if (param_or(action, 0, 0) == 5) {
            /* Status report — "OK" */
            queue_response(t, "\x1b[0n");
        }
        break;
    case 'c': /* DA — device attributes */
        if (priv) {
            /* DA2 — secondary device attributes */
            queue_response(t, "\x1b[>1;1;0c");
        } else {
            /* DA1 — primary device attributes (VT220) */
            queue_response(t, "\x1b[?62;22c");
        }
        break;
    case 'S': /* SU — scroll up */
        if (!priv) {
            wixen_grid_scroll_region_up(&t->grid, effective_top(t),
                                        effective_bottom(t), param_or(action, 0, 1));
        }
        break;
    case 'T': /* SD — scroll down */
        if (!priv) {
            wixen_grid_scroll_region_down(&t->grid, effective_top(t),
                                          effective_bottom(t), param_or(action, 0, 1));
        }
        break;
    case 'q': /* DECSCUSR — set cursor style */
        if (action->csi.intermediate_count > 0 && action->csi.intermediates[0] == ' ') {
            uint16_t style = (uint16_t)param_or(action, 0, 0);
            switch (style) {
            case 0: case 1: t->grid.cursor.shape = WIXEN_CURSOR_BLOCK; t->grid.cursor.blinking = true; break;
            case 2: t->grid.cursor.shape = WIXEN_CURSOR_BLOCK; t->grid.cursor.blinking = false; break;
            case 3: t->grid.cursor.shape = WIXEN_CURSOR_UNDERLINE; t->grid.cursor.blinking = true; break;
            case 4: t->grid.cursor.shape = WIXEN_CURSOR_UNDERLINE; t->grid.cursor.blinking = false; break;
            case 5: t->grid.cursor.shape = WIXEN_CURSOR_BAR; t->grid.cursor.blinking = true; break;
            case 6: t->grid.cursor.shape = WIXEN_CURSOR_BAR; t->grid.cursor.blinking = false; break;
            }
        }
        break;
    }
}

/* --- ESC dispatch --- */

static void terminal_esc(WixenTerminal *t, const WixenAction *action) {
    char final = action->esc.final_byte;

    if (action->esc.intermediate_count == 0) {
        switch (final) {
        case 'D': wixen_terminal_index(t); break;
        case 'M': wixen_terminal_reverse_index(t); break;
        case 'E': /* NEL — next line */
            wixen_terminal_carriage_return(t);
            wixen_terminal_linefeed(t);
            break;
        case '7': wixen_terminal_save_cursor(t); break;
        case '8': wixen_terminal_restore_cursor(t); break;
        case 'c': wixen_terminal_reset(t); break;
        case 'H': wixen_terminal_set_tab_stop(t); break;
        case '=': t->modes.keypad_application = true; break;  /* DECKPAM */
        case '>': t->modes.keypad_application = false; break; /* DECKPNM */
        case 'N': /* SS2 — single shift G2 (next char only) */
        case 'O': /* SS3 — single shift G3 (next char only) */
            /* These are rarely used. For now, ignored. */
            break;
        }
    } else if (action->esc.intermediate_count == 1) {
        uint8_t inter = action->esc.intermediates[0];
        /* Charset designation: ESC ( X or ESC ) X */
        if (inter == '(' || inter == ')') {
            int gset = (inter == '(') ? 0 : 1;
            switch (final) {
            case 'B': t->charsets[gset] = 0; break; /* ASCII */
            case '0': t->charsets[gset] = 1; break; /* DEC Special Graphics (box drawing) */
            case 'A': t->charsets[gset] = 2; break; /* UK */
            default: break;
            }
        }
        if (inter == '#' && final == '8') {
            /* DECALN — fill screen with 'E' */
            for (size_t r = 0; r < t->grid.num_rows; r++) {
                for (size_t c = 0; c < t->grid.cols; c++) {
                    WixenCell *cell = wixen_grid_cell(&t->grid, c, r);
                    if (cell) wixen_cell_set_content(cell, "E");
                }
            }
            t->dirty = true;
        }
    }
}

/* --- Main dispatch --- */

void wixen_terminal_dispatch(WixenTerminal *t, const WixenAction *action) {
    switch (action->type) {
    case WIXEN_ACTION_PRINT:
        terminal_print(t, action->codepoint);
        break;
    case WIXEN_ACTION_EXECUTE:
        terminal_execute(t, action->control_byte);
        break;
    case WIXEN_ACTION_CSI_DISPATCH:
        terminal_csi(t, action);
        break;
    case WIXEN_ACTION_ESC_DISPATCH:
        terminal_esc(t, action);
        break;
    case WIXEN_ACTION_OSC_DISPATCH:
        terminal_osc(t, action->osc.data, action->osc.data_len);
        break;
    case WIXEN_ACTION_DCS_HOOK:
        if (action->dcs_hook.final_byte == 'q') {
            if (action->dcs_hook.intermediate_count > 0 &&
                action->dcs_hook.intermediates[0] == '$') {
                /* DECRQSS — request status string */
                /* Respond with valid setting for common queries */
                /* For now, just acknowledge */
            } else {
                /* Sixel graphics */
                t->in_sixel = true;
            }
        }
        break;
    case WIXEN_ACTION_DCS_PUT:
        /* Sixel data or other DCS data — ignore for now */
        break;
    case WIXEN_ACTION_DCS_UNHOOK:
        t->in_sixel = false;
        break;
    case WIXEN_ACTION_APC_DISPATCH:
        /* Kitty graphics protocol uses APC — not yet implemented */
        break;
    case WIXEN_ACTION_NONE:
        break;
    }
}
