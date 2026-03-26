/* events.c — Event throttling and VT escape stripping */
#include "wixen/a11y/events.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = (char *)malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

void wixen_throttler_init(WixenEventThrottler *et, uint32_t debounce_ms) {
    memset(et, 0, sizeof(*et));
    et->debounce_ms = debounce_ms > 0 ? debounce_ms : 100;
    /* Ensure first event always fires by setting last_event far in the past */
    et->last_event_ms = 0;
    /* We'll use a flag instead */
}

void wixen_throttler_free(WixenEventThrottler *et) {
    free(et->pending_text);
    memset(et, 0, sizeof(*et));
}

bool wixen_throttler_on_output(WixenEventThrottler *et, const char *text,
                                uint64_t now_ms) {
    if (!text || !text[0]) return false;

    size_t len = strlen(text);

    /* Append to pending buffer */
    if (et->pending_len + len >= et->pending_cap) {
        size_t new_cap = (et->pending_cap + len + 1) * 2;
        if (new_cap < 256) new_cap = 256;
        char *new_buf = (char *)realloc(et->pending_text, new_cap);
        if (!new_buf) return false;
        et->pending_text = new_buf;
        et->pending_cap = new_cap;
    }
    memcpy(et->pending_text + et->pending_len, text, len);
    et->pending_len += len;
    et->pending_text[et->pending_len] = '\0';

    /* Count lines */
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') et->lines_in_batch++;
    }

    /* Detect streaming mode (>10 lines per batch) */
    et->streaming = et->lines_in_batch > 10;

    /* Check if debounce window has elapsed, or this is the first output */
    bool first_output = (et->last_event_ms == 0 && et->pending_len > 0);
    if (first_output || (now_ms - et->last_event_ms >= et->debounce_ms)) {
        et->last_event_ms = now_ms > 0 ? now_ms : 1; /* Avoid 0 for "never fired" sentinel */
        return true; /* Fire event */
    }

    return false;
}

char *wixen_throttler_take_pending(WixenEventThrottler *et) {
    if (et->pending_len == 0) return NULL;
    char *text = et->pending_text;
    et->pending_text = NULL;
    et->pending_len = 0;
    et->pending_cap = 0;
    et->lines_in_batch = 0;
    return text;
}

bool wixen_throttler_is_streaming(const WixenEventThrottler *et) {
    return et->streaming;
}

/* --- VT escape stripping --- */

char *wixen_strip_vt_escapes(const char *text) {
    if (!text) return dup_str("");
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    if (!out) return dup_str("");

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\x1b') {
            i++; /* Skip ESC */
            if (i >= len) break;
            if (text[i] == '[') {
                /* CSI — skip until final byte (0x40-0x7E) */
                i++;
                while (i < len && (text[i] < 0x40 || text[i] > 0x7E)) i++;
                /* i now points to final byte, loop will increment past it */
            } else if (text[i] == ']') {
                /* OSC — skip until BEL or ST */
                i++;
                while (i < len && text[i] != '\x07' && text[i] != '\x1b') i++;
                if (i < len && text[i] == '\x1b') i++; /* Skip ESC of ST */
            } else if (text[i] == 'P' || text[i] == '_' || text[i] == '^') {
                /* DCS, APC, PM — skip until ST */
                i++;
                while (i < len && text[i] != '\x1b') i++;
                if (i < len) i++; /* Skip ESC of ST */
            }
            /* Single-char escape: already skipped */
        } else {
            out[j++] = text[i];
        }
    }
    out[j] = '\0';
    return out;
}

char *wixen_strip_control_chars(const char *text) {
    if (!text) return dup_str("");
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    if (!out) return dup_str("");

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x20 || c == '\n' || c == '\t') {
            out[j++] = text[i];
        }
        /* Skip all other control chars (0x00-0x1F except \n and \t) */
    }
    out[j] = '\0';
    return out;
}

/* --- Prompt stripping --- */

#ifdef _MSC_VER
#define strdup _strdup
#endif

char *wixen_strip_prompt(const char *line) {
    if (!line || !line[0]) return strdup("");

    /* PS C:\path> command */
    if (strncmp(line, "PS ", 3) == 0) {
        const char *gt = strchr(line + 3, '>');
        if (gt) {
            const char *cmd = gt + 1;
            while (*cmd == ' ') cmd++;
            return strdup(cmd);
        }
    }

    /* C:\path> command (cmd.exe) */
    {
        const char *gt = strchr(line, '>');
        if (gt && gt > line) {
            /* Check if everything before > looks like a path (contains : or \) */
            bool looks_like_path = false;
            for (const char *p = line; p < gt; p++) {
                if (*p == ':' || *p == '\\' || *p == '/') {
                    looks_like_path = true;
                    break;
                }
            }
            if (looks_like_path) {
                const char *cmd = gt + 1;
                while (*cmd == ' ') cmd++;
                return strdup(cmd);
            }
        }
    }

    /* $ command (bash/zsh) */
    if (line[0] == '$' && line[1] == ' ') {
        return strdup(line + 2);
    }

    /* # command (root shell) */
    if (line[0] == '#' && line[1] == ' ') {
        return strdup(line + 2);
    }

    /* No recognized prompt — return as-is */
    return strdup(line);
}

/* --- Announcement formatting --- */

char *wixen_a11y_format_command_complete(const char *command, int exit_code) {
    const char *cmd = command ? command : "command";
    size_t len = strlen(cmd) + 48;
    char *buf = malloc(len);
    if (!buf) return NULL;
    if (exit_code == 0) {
        snprintf(buf, len, "%s succeeded", cmd);
    } else {
        snprintf(buf, len, "%s failed (exit %d)", cmd, exit_code);
    }
    return buf;
}

char *wixen_a11y_format_mode_change(const char *mode, bool enabled) {
    const char *state = enabled ? "on" : "off";
    size_t len = strlen(mode) + 16;
    char *buf = malloc(len);
    if (!buf) return NULL;
    snprintf(buf, len, "%s %s", mode, state);
    return buf;
}

char *wixen_a11y_format_image_placed(int width, int height, const char *name) {
    size_t len = 128 + (name ? strlen(name) : 0);
    char *buf = malloc(len);
    if (!buf) return NULL;
    if (name) {
        snprintf(buf, len, "image placed: %s (%dx%d)", name, width, height);
    } else {
        snprintf(buf, len, "image placed (%dx%d)", width, height);
    }
    return buf;
}

/* --- TextChanged event logic --- */

bool wixen_a11y_should_raise_text_changed(const char *old_text, const char *new_text) {
    /* Normalize NULL to empty string for comparison */
    const char *a = old_text ? old_text : "";
    const char *b = new_text ? new_text : "";
    return strcmp(a, b) != 0;
}
