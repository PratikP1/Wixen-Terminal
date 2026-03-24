/* error_detect.c — Output line classification */
#include "wixen/core/error_detect.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* Case-insensitive substring check */
static bool contains_ci(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        bool match = true;
        for (size_t i = 0; i < nlen; i++) {
            if (!p[i] || tolower((unsigned char)p[i]) != tolower((unsigned char)needle[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/* Check if a word appears at word boundary (preceded by space/start, followed by space/colon/end) */
static bool has_word(const char *line, const char *word) {
    size_t wlen = strlen(word);
    const char *p = line;
    while ((p = strstr(p, word)) != NULL) {
        bool at_start = (p == line) || !isalnum((unsigned char)p[-1]);
        bool at_end = (p[wlen] == '\0') || p[wlen] == ':' || p[wlen] == ' '
                    || p[wlen] == ']' || p[wlen] == ')';
        if (at_start && at_end) return true;
        p++;
    }
    return false;
}

WixenOutputLineClass wixen_classify_output_line(const char *line) {
    if (!line || !line[0]) return WIXEN_LINE_NORMAL;

    /* Error patterns */
    if (has_word(line, "error") || has_word(line, "Error") || has_word(line, "ERROR")
        || has_word(line, "FAILED") || has_word(line, "FAIL")
        || has_word(line, "fatal") || has_word(line, "Fatal") || has_word(line, "FATAL")
        || has_word(line, "panic") || has_word(line, "PANIC")
        || contains_ci(line, "panicked")
        || contains_ci(line, "ERR!")          /* npm ERR! */
        || contains_ci(line, "Traceback")     /* Python traceback */
        || contains_ci(line, "Exception")) {  /* Java/Python exceptions */
        return WIXEN_LINE_ERROR;
    }

    /* Compiler error patterns */
    if (strstr(line, "error[E") != NULL) return WIXEN_LINE_ERROR;   /* Rust: error[E0308] */
    if (strstr(line, "error C") != NULL) return WIXEN_LINE_ERROR;   /* MSVC: error C2220 */
    if (strstr(line, ": error:") != NULL) return WIXEN_LINE_ERROR;  /* GCC/Clang */

    /* Caret line (compiler error indicator) */
    {
        const char *p = line;
        while (*p == ' ') p++;
        bool all_carets = (*p == '^');
        while (*p == '^' || *p == '~') p++;
        while (*p == ' ') p++;
        if (all_carets && *p == '\0' && p - line > 1) return WIXEN_LINE_ERROR;
    }

    /* Warning patterns */
    if (has_word(line, "warning") || has_word(line, "Warning") || has_word(line, "WARNING")
        || has_word(line, "WARN") || has_word(line, "warn")
        || contains_ci(line, "deprecated")) {
        return WIXEN_LINE_WARNING;
    }

    /* Info patterns */
    if (has_word(line, "note") || has_word(line, "Note") || has_word(line, "NOTE")
        || has_word(line, "info") || has_word(line, "Info") || has_word(line, "INFO")
        || has_word(line, "hint") || has_word(line, "Hint")) {
        return WIXEN_LINE_INFO;
    }

    return WIXEN_LINE_NORMAL;
}

void wixen_count_errors_warnings(const char **lines, size_t count,
                                  size_t *out_errors, size_t *out_warnings) {
    size_t errors = 0, warnings = 0;
    for (size_t i = 0; i < count; i++) {
        WixenOutputLineClass c = wixen_classify_output_line(lines[i]);
        if (c == WIXEN_LINE_ERROR) errors++;
        else if (c == WIXEN_LINE_WARNING) warnings++;
    }
    if (out_errors) *out_errors = errors;
    if (out_warnings) *out_warnings = warnings;
}

int wixen_detect_progress(const char *line) {
    if (!line) return -1;

    /* Look for N% pattern */
    const char *p = line;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            int val = 0;
            const char *start = p;
            while (isdigit((unsigned char)*p)) {
                val = val * 10 + (*p - '0');
                p++;
            }
            if (*p == '%' && val >= 0 && val <= 100 && (start == line || !isalpha((unsigned char)start[-1]))) {
                return val;
            }
        } else {
            p++;
        }
    }

    /* Look for N/M pattern (e.g., "5/10" or "5 of 10") */
    p = line;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            int num = atoi(p);
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '/') {
                p++;
                if (isdigit((unsigned char)*p)) {
                    int den = atoi(p);
                    if (den > 0 && num <= den) {
                        return (int)((num * 100) / den);
                    }
                }
            } else if (strncmp(p, " of ", 4) == 0) {
                p += 4;
                if (isdigit((unsigned char)*p)) {
                    int den = atoi(p);
                    if (den > 0 && num <= den) {
                        return (int)((num * 100) / den);
                    }
                }
            }
        } else {
            p++;
        }
    }

    return -1;
}
