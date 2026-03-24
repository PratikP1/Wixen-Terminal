/* cursor.h — Terminal cursor state */
#ifndef WIXEN_CORE_CURSOR_H
#define WIXEN_CORE_CURSOR_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WIXEN_CURSOR_BLOCK = 0,
    WIXEN_CURSOR_UNDERLINE,
    WIXEN_CURSOR_BAR,
} WixenCursorShape;

typedef struct {
    size_t col;
    size_t row;
    bool visible;
    WixenCursorShape shape;
    bool blinking;
} WixenCursor;

void wixen_cursor_init(WixenCursor *cur);
void wixen_cursor_move_to(WixenCursor *cur, size_t col, size_t row);
void wixen_cursor_clamp(WixenCursor *cur, size_t max_col, size_t max_row);

#endif /* WIXEN_CORE_CURSOR_H */
