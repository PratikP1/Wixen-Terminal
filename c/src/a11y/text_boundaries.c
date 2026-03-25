/* text_boundaries.c — Pure text boundary logic for UIA text ranges
 *
 * No Win32 dependency — usable in tests on any platform.
 */
#include "wixen/a11y/text_boundaries.h"
#include <string.h>
#include <ctype.h>

void wixen_text_line_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end) {
    if (!text || text_len == 0 || offset > text_len) {
        *out_start = *out_end = 0;
        return;
    }
    if (offset >= text_len) offset = text_len > 0 ? text_len - 1 : 0;

    /* Find start of line (scan backward for \n) */
    size_t start = offset;
    while (start > 0 && text[start - 1] != '\n') start--;

    /* Find end of line (scan forward for \n or end) */
    size_t end = offset;
    while (end < text_len && text[end] != '\n') end++;

    *out_start = start;
    *out_end = end;
}

void wixen_text_word_at(const char *text, size_t text_len, size_t offset,
                         size_t *out_start, size_t *out_end) {
    if (!text || text_len == 0 || offset >= text_len) {
        *out_start = *out_end = offset;
        return;
    }

    /* Determine if we're in a word char or whitespace */
    int in_word = isalnum((unsigned char)text[offset]) || text[offset] == '_';

    /* Scan backward to start of word/space run */
    size_t start = offset;
    while (start > 0) {
        int prev_is_word = isalnum((unsigned char)text[start - 1]) || text[start - 1] == '_';
        if (prev_is_word != in_word) break;
        start--;
    }

    /* Scan forward to end of word/space run */
    size_t end = offset;
    while (end < text_len) {
        int cur_is_word = isalnum((unsigned char)text[end]) || text[end] == '_';
        if (cur_is_word != in_word) break;
        end++;
    }

    *out_start = start;
    *out_end = end;
}

void wixen_text_offset_to_rowcol(const char *text, size_t text_len, size_t offset,
                                   size_t *out_row, size_t *out_col) {
    size_t row = 0, col = 0;
    for (size_t i = 0; i < text_len && i < offset; i++) {
        if (text[i] == '\n') {
            row++;
            col = 0;
        } else {
            col++;
        }
    }
    *out_row = row;
    *out_col = col;
}

size_t wixen_text_rowcol_to_offset(const char *text, size_t text_len,
                                     size_t row, size_t col) {
    size_t r = 0, c = 0;
    for (size_t i = 0; i < text_len; i++) {
        if (r == row && c == col) return i;
        if (text[i] == '\n') {
            if (r == row) return i; /* col beyond line length */
            r++;
            c = 0;
        } else {
            c++;
        }
    }
    return text_len;
}

/* --- Individual boundary queries --- */

size_t wixen_text_word_start(const char *text, size_t text_len, size_t offset) {
    size_t s, e;
    wixen_text_word_at(text, text_len, offset, &s, &e);
    return s;
}

size_t wixen_text_word_end(const char *text, size_t text_len, size_t offset) {
    size_t s, e;
    wixen_text_word_at(text, text_len, offset, &s, &e);
    return e;
}

size_t wixen_text_line_start(const char *text, size_t text_len, size_t offset) {
    size_t s, e;
    wixen_text_line_at(text, text_len, offset, &s, &e);
    return s;
}

size_t wixen_text_line_end(const char *text, size_t text_len, size_t offset) {
    size_t s, e;
    wixen_text_line_at(text, text_len, offset, &s, &e);
    return e;
}

/* --- Move by unit --- */

int wixen_text_move_by_char(const char *text, size_t text_len, size_t *pos, int count) {
    (void)text;
    int moved = 0;
    if (count > 0) {
        while (moved < count && *pos < text_len) {
            (*pos)++;
            moved++;
        }
    } else {
        while (moved > count && *pos > 0) {
            (*pos)--;
            moved--;
        }
    }
    return moved;
}

int wixen_text_move_by_word(const char *text, size_t text_len, size_t *pos, int count) {
    int moved = 0;
    if (count > 0) {
        while (moved < count && *pos < text_len) {
            /* Skip current word */
            while (*pos < text_len && !isspace((unsigned char)text[*pos])) (*pos)++;
            /* Skip whitespace */
            while (*pos < text_len && isspace((unsigned char)text[*pos])) (*pos)++;
            moved++;
        }
    } else {
        while (moved > count && *pos > 0) {
            /* Skip whitespace backward */
            while (*pos > 0 && isspace((unsigned char)text[*pos - 1])) (*pos)--;
            /* Skip word backward */
            while (*pos > 0 && !isspace((unsigned char)text[*pos - 1])) (*pos)--;
            moved--;
        }
    }
    return moved;
}

int wixen_text_move_by_line(const char *text, size_t text_len, size_t *pos, int count) {
    int moved = 0;
    if (count > 0) {
        while (moved < count && *pos < text_len) {
            /* Find next newline */
            while (*pos < text_len && text[*pos] != '\n') (*pos)++;
            if (*pos < text_len) (*pos)++; /* Skip past newline */
            moved++;
        }
    } else {
        while (moved > count && *pos > 0) {
            /* Move to start of current line */
            if (*pos > 0) (*pos)--;
            while (*pos > 0 && text[*pos - 1] != '\n') (*pos)--;
            moved--;
        }
    }
    return moved;
}

/* --- Find text --- */

bool wixen_text_find(const char *text, size_t text_len, const char *query,
                      size_t from_offset, bool ignore_case,
                      size_t *out_start, size_t *out_end) {
    if (!text || !query || !query[0]) return false;
    size_t qlen = strlen(query);
    if (qlen > text_len) return false;

    for (size_t i = from_offset; i + qlen <= text_len; i++) {
        bool match = true;
        for (size_t j = 0; j < qlen; j++) {
            char a = text[i + j];
            char b = query[j];
            if (ignore_case) {
                a = (char)tolower((unsigned char)a);
                b = (char)tolower((unsigned char)b);
            }
            if (a != b) { match = false; break; }
        }
        if (match) {
            if (out_start) *out_start = i;
            if (out_end) *out_end = i + qlen;
            return true;
        }
    }
    return false;
}
