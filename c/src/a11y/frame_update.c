/* frame_update.c — A11y frame loop integration logic
 *
 * Mirrors the Rust main.rs a11y block: throttle output, detect cursor
 * movement, detect line replacement, strip prompts, suppress echo.
 */
#include "wixen/a11y/frame_update.h"
#include "wixen/a11y/events.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#define strdup _strdup
#endif

void wixen_frame_a11y_init(WixenFrameA11yState *fs, uint32_t debounce_ms) {
    memset(fs, 0, sizeof(*fs));
    fs->debounce_ms = debounce_ms;
}

void wixen_frame_a11y_free(WixenFrameA11yState *fs) {
    free(fs->prev_visible_text);
    free(fs->prev_cursor_line);
    free(fs->pending_output);
    memset(fs, 0, sizeof(*fs));
}

/* Extract the line at a given row from newline-delimited text */
static char *extract_line(const char *text, size_t row) {
    if (!text) return strdup("");
    const char *p = text;
    size_t r = 0;
    while (r < row && *p) {
        if (*p == '\n') r++;
        p++;
    }
    /* p now points to start of target row */
    const char *end = p;
    while (*end && *end != '\n') end++;
    size_t len = (size_t)(end - p);
    char *line = malloc(len + 1);
    if (line) { memcpy(line, p, len); line[len] = '\0'; }
    return line;
}

/* Trim trailing spaces */
static void trim_right(char *s) {
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

/* Check if change is append (cur starts with prev) */
static bool is_append(const char *prev, const char *cur) {
    size_t plen = strlen(prev);
    size_t clen = strlen(cur);
    return clen > plen && strncmp(cur, prev, plen) == 0;
}

/* Check if change is deletion (prev starts with cur) */
static bool is_deletion(const char *prev, const char *cur) {
    size_t plen = strlen(prev);
    size_t clen = strlen(cur);
    return clen < plen && strncmp(prev, cur, clen) == 0;
}

void wixen_frame_a11y_update(WixenFrameA11yState *fs,
                              const WixenFrameA11yInput *input,
                              WixenFrameA11yEvents *events) {
    memset(events, 0, sizeof(*events));

    /* --- Text change detection --- */
    const char *cur_text = input->visible_text ? input->visible_text : "";
    if (!fs->prev_visible_text || strcmp(fs->prev_visible_text, cur_text) != 0) {
        events->text_changed = true;
    }

    /* --- Cursor movement --- */
    if (fs->initialized) {
        if (fs->prev_cursor_row != input->cursor_row ||
            fs->prev_cursor_col != input->cursor_col) {
            events->cursor_moved = true;
        }
        if (fs->prev_cursor_row != input->cursor_row) {
            events->row_changed = true;
        }
    }

    /* --- Structure change (shell integ generation) --- */
    if (fs->initialized && fs->prev_shell_generation != input->shell_generation) {
        events->structure_changed = true;
    }

    /* --- Output throttling + echo suppression --- */
    bool has_real_output = false;
    if (input->has_new_output && input->new_output_text) {
        /* Strip VT escapes */
        char *cleaned = wixen_strip_vt_escapes(input->new_output_text);
        if (cleaned) {
            char *trimmed = wixen_strip_control_chars(cleaned);
            free(cleaned);
            if (trimmed && strlen(trimmed) > 0) {
                /* Only announce multi-line output (contains newline).
                 * Single chars without newline = keyboard echo — let NVDA handle. */
                if (strchr(trimmed, '\n') != NULL) {
                    events->should_announce_output = true;
                    events->announce_text = trimmed;
                    has_real_output = true;
                } else {
                    free(trimmed);
                }
            } else {
                free(trimmed);
            }
        }
    }

    /* --- Line replacement detection (history recall) --- */
    char *current_line = extract_line(cur_text, input->cursor_row);
    if (current_line) trim_right(current_line);

    if (fs->initialized && !has_real_output && !events->structure_changed &&
        fs->prev_cursor_line && current_line) {

        char *prev_trimmed = strdup(fs->prev_cursor_line);
        if (prev_trimmed) {
            trim_right(prev_trimmed);
            char *cur_trimmed = strdup(current_line);
            if (cur_trimmed) {
                trim_right(cur_trimmed);

                if (strlen(prev_trimmed) > 0 && strcmp(prev_trimmed, cur_trimmed) != 0) {
                    /* Line content changed — is it replacement (not append/delete)? */
                    if (!is_append(prev_trimmed, cur_trimmed) &&
                        !is_deletion(prev_trimmed, cur_trimmed)) {
                        /* Line was replaced (history recall) */
                        char *stripped = wixen_strip_prompt(cur_trimmed);
                        if (stripped) {
                            events->should_announce_line = true;
                            events->announce_line_text = stripped;
                        }
                    }
                }
                free(cur_trimmed);
            }
            free(prev_trimmed);
        }
    }

    /* --- Password prompt detection --- */
    if (input->echo_timeout_detected) {
        events->password_prompt = true;
    }

    /* --- Compute cursor offset (simplified UTF-8 byte offset) --- */
    {
        int32_t offset = 0;
        const char *p = cur_text;
        size_t r = 0;
        while (r < input->cursor_row && *p) {
            if (*p == '\n') r++;
            p++;
            offset++;
        }
        size_t c = 0;
        while (c < input->cursor_col && *p && *p != '\n') {
            p++; c++; offset++;
        }
        events->cursor_offset_utf16 = offset;
        fs->cursor_offset_utf16 = offset;
    }

    /* --- Update state for next frame --- */
    free(fs->prev_visible_text);
    fs->prev_visible_text = strdup(cur_text);
    free(fs->prev_cursor_line);
    fs->prev_cursor_line = current_line; /* Ownership transferred */
    fs->prev_cursor_row = input->cursor_row;
    fs->prev_cursor_col = input->cursor_col;
    fs->prev_shell_generation = input->shell_generation;
    fs->initialized = true;
}
