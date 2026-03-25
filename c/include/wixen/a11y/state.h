/* state.h — Thread-safe accessibility state
 *
 * Shared between the main thread (writer) and UIA threads (readers).
 * Uses SRWLOCK for concurrent read access with exclusive writes.
 * Cursor offset is lock-free via InterlockedExchange for GetSelection.
 */
#ifndef WIXEN_A11Y_STATE_H
#define WIXEN_A11Y_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WixenA11yState WixenA11yState;

/* Lifecycle */
WixenA11yState *wixen_a11y_state_create(void);
void wixen_a11y_state_destroy(WixenA11yState *state);

/* Writer (main thread) */
void wixen_a11y_state_update_cursor(WixenA11yState *state, size_t row, size_t col);
void wixen_a11y_state_set_text(WixenA11yState *state, const char *text);

/* Reader (UIA threads — lock-free or read-locked) */
int32_t wixen_a11y_state_cursor_offset(const WixenA11yState *state);
size_t wixen_a11y_state_get_text(const WixenA11yState *state, char *buf, size_t buf_size);

#endif /* WIXEN_A11Y_STATE_H */
