/* frame_update.h — A11y frame loop integration logic
 *
 * Encapsulates all per-frame accessibility decisions:
 * - Cursor tracking + text change detection
 * - Output throttling + echo suppression
 * - Line replacement detection + prompt stripping
 * - Structure change detection
 * - Password prompt detection
 *
 * Pure logic — no Win32 or UIA dependency. The caller reads the
 * output events and translates them into UIA API calls.
 */
#ifndef WIXEN_A11Y_FRAME_UPDATE_H
#define WIXEN_A11Y_FRAME_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Persistent state across frames */
typedef struct {
    /* Previous frame state */
    size_t prev_cursor_row;
    size_t prev_cursor_col;
    char *prev_visible_text;
    char *prev_cursor_line;
    uint64_t prev_shell_generation;

    /* Throttler */
    char *pending_output;
    size_t pending_output_len;
    size_t pending_output_cap;
    uint64_t last_output_event_ms;
    uint32_t debounce_ms;

    /* Atomic cursor offset (for lock-free GetSelection) */
    int32_t cursor_offset_utf16;

    bool initialized;
} WixenFrameA11yState;

/* Input for one frame */
typedef struct {
    size_t cursor_row;
    size_t cursor_col;
    const char *visible_text;
    uint64_t shell_generation;
    bool has_new_output;
    const char *new_output_text;
    uint64_t now_ms;
    bool echo_timeout_detected;
} WixenFrameA11yInput;

/* Output events from one frame */
typedef struct {
    bool cursor_moved;
    bool row_changed;
    bool text_changed;
    bool structure_changed;
    bool should_announce_output;
    char *announce_text;         /* Heap-allocated. Caller frees. */
    bool should_announce_line;
    char *announce_line_text;    /* Heap-allocated, prompt-stripped. Caller frees. */
    bool password_prompt;
    int32_t cursor_offset_utf16;
} WixenFrameA11yEvents;

void wixen_frame_a11y_init(WixenFrameA11yState *fs, uint32_t debounce_ms);
void wixen_frame_a11y_free(WixenFrameA11yState *fs);
void wixen_frame_a11y_update(WixenFrameA11yState *fs,
                              const WixenFrameA11yInput *input,
                              WixenFrameA11yEvents *events);

#endif /* WIXEN_A11Y_FRAME_UPDATE_H */
