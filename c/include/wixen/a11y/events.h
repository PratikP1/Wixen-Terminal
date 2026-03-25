/* events.h — UIA event throttling for screen reader output */
#ifndef WIXEN_A11Y_EVENTS_H
#define WIXEN_A11Y_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Event throttler — batches terminal output into debounced windows */
typedef struct {
    char *pending_text;       /* Accumulated text */
    size_t pending_len;
    size_t pending_cap;
    uint64_t last_event_ms;   /* Timestamp of last raised event */
    uint32_t debounce_ms;     /* Debounce window (default 100ms) */
    size_t lines_in_batch;    /* Lines accumulated in current batch */
    bool streaming;           /* Rapid output detected */
} WixenEventThrottler;

void wixen_throttler_init(WixenEventThrottler *et, uint32_t debounce_ms);
void wixen_throttler_free(WixenEventThrottler *et);

/* Called when new terminal output arrives. Returns true if event should fire. */
bool wixen_throttler_on_output(WixenEventThrottler *et, const char *text,
                                uint64_t now_ms);

/* Get accumulated pending text and clear it. Returns NULL if empty.
 * Caller must free the returned string. */
char *wixen_throttler_take_pending(WixenEventThrottler *et);

/* Check if in streaming mode (rapid output) */
bool wixen_throttler_is_streaming(const WixenEventThrottler *et);

/* Strip VT escape sequences from text for screen reader.
 * Returns heap-allocated cleaned text. Caller frees. */
char *wixen_strip_vt_escapes(const char *text);

/* Strip control characters (0x00-0x1F except \n) from text.
 * Returns heap-allocated cleaned text. Caller frees. */
char *wixen_strip_control_chars(const char *text);

/* Strip shell prompt prefix from a line. Returns heap-allocated text. Caller frees.
 * Recognizes: "C:\path>", "PS C:\path>", "$ ", "# " */
char *wixen_strip_prompt(const char *line);

/* Format a command completion announcement. Caller frees. */
char *wixen_a11y_format_command_complete(const char *command, int exit_code);

/* Format a mode change announcement. Caller frees. */
char *wixen_a11y_format_mode_change(const char *mode, bool enabled);

/* Format an image placed announcement. Caller frees. */
char *wixen_a11y_format_image_placed(int width, int height, const char *name);

#endif /* WIXEN_A11Y_EVENTS_H */
