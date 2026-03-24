/* cursor.c — Terminal cursor implementation */
#include "wixen/core/cursor.h"

void wixen_cursor_init(WixenCursor *cur) {
    cur->col = 0;
    cur->row = 0;
    cur->visible = true;
    cur->shape = WIXEN_CURSOR_BLOCK;
    cur->blinking = true;
}

void wixen_cursor_move_to(WixenCursor *cur, size_t col, size_t row) {
    cur->col = col;
    cur->row = row;
}

void wixen_cursor_clamp(WixenCursor *cur, size_t max_col, size_t max_row) {
    if (cur->col >= max_col && max_col > 0) cur->col = max_col - 1;
    if (cur->row >= max_row && max_row > 0) cur->row = max_row - 1;
}
