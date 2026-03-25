/* state.c — Thread-safe accessibility state with SRWLOCK */
#ifdef _WIN32

#include "wixen/a11y/state.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define strdup _strdup
#endif

struct WixenA11yState {
    SRWLOCK lock;
    /* Protected by lock */
    char *full_text;
    size_t text_len;
    size_t cursor_row;
    size_t cursor_col;
    /* Lock-free cursor offset for GetSelection/GetCaretRange */
    volatile LONG cursor_offset_utf16;
};

WixenA11yState *wixen_a11y_state_create(void) {
    WixenA11yState *s = calloc(1, sizeof(WixenA11yState));
    if (!s) return NULL;
    InitializeSRWLock(&s->lock);
    s->full_text = strdup("");
    return s;
}

void wixen_a11y_state_destroy(WixenA11yState *state) {
    if (!state) return;
    free(state->full_text);
    free(state);
}

void wixen_a11y_state_update_cursor(WixenA11yState *state, size_t row, size_t col) {
    AcquireSRWLockExclusive(&state->lock);
    state->cursor_row = row;
    state->cursor_col = col;
    /* Compute UTF-16 offset for lock-free access */
    /* Walk text to find byte offset of (row, col), then count UTF-16 units */
    int32_t offset = 0;
    if (state->full_text) {
        const char *p = state->full_text;
        size_t r = 0;
        while (r < row && *p) {
            if (*p == '\n') r++;
            p++;
        }
        /* Now at start of target row. Advance col positions. */
        size_t c = 0;
        while (c < col && *p && *p != '\n') {
            p++;
            c++;
        }
        /* Count UTF-16 units from start to current position */
        offset = (int32_t)(p - state->full_text);
    }
    ReleaseSRWLockExclusive(&state->lock);
    InterlockedExchange(&state->cursor_offset_utf16, offset);
}

void wixen_a11y_state_set_text(WixenA11yState *state, const char *text) {
    char *copy = text ? strdup(text) : strdup("");
    AcquireSRWLockExclusive(&state->lock);
    free(state->full_text);
    state->full_text = copy;
    state->text_len = copy ? strlen(copy) : 0;
    ReleaseSRWLockExclusive(&state->lock);
}

int32_t wixen_a11y_state_cursor_offset(const WixenA11yState *state) {
    /* Lock-free read */
    return InterlockedCompareExchange(
        (volatile LONG *)&state->cursor_offset_utf16, 0, 0);
}

size_t wixen_a11y_state_get_text(const WixenA11yState *state, char *buf, size_t buf_size) {
    AcquireSRWLockShared((PSRWLOCK)&state->lock);
    size_t len = state->text_len;
    if (len >= buf_size) len = buf_size - 1;
    if (len > 0 && state->full_text) {
        memcpy(buf, state->full_text, len);
    }
    buf[len] = '\0';
    ReleaseSRWLockShared((PSRWLOCK)&state->lock);
    return len;
}

#endif /* _WIN32 */
