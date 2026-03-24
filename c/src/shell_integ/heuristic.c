/* heuristic.c — Heuristic prompt detection */
#include "wixen/shell_integ/heuristic.h"
#include <string.h>
#include <ctype.h>

/* Common prompt ending characters */
static bool is_prompt_end_char(char c) {
    return c == '>' || c == '$' || c == '#' || c == '%';
}

/* Check for common prompt patterns */
bool wixen_is_prompt_line(const char *line) {
    if (!line || !line[0]) return false;
    size_t len = strlen(line);

    /* Trim trailing whitespace */
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) len--;
    if (len == 0) return false;

    char last = line[len - 1];

    /* Ends with prompt character */
    if (is_prompt_end_char(last)) return true;

    /* Special unicode prompt chars (UTF-8) */
    /* ❯ = E2 9D AF */
    if (len >= 3 && (unsigned char)line[len - 3] == 0xE2
        && (unsigned char)line[len - 2] == 0x9D
        && (unsigned char)line[len - 1] == 0xAF) return true;

    /* Windows cmd.exe: "C:\path>" */
    if (last == '>' && len >= 3) {
        /* Check for drive letter pattern */
        if (isalpha((unsigned char)line[0]) && line[1] == ':') return true;
    }

    /* PowerShell: "PS C:\path>" */
    if (len >= 5 && line[0] == 'P' && line[1] == 'S' && line[2] == ' ') {
        if (last == '>') return true;
    }

    return false;
}

size_t wixen_prompt_end_col(const char *line) {
    if (!line) return 0;
    size_t len = strlen(line);

    /* Find last prompt-ending character */
    for (size_t i = len; i > 0; i--) {
        if (is_prompt_end_char(line[i - 1])) {
            /* The prompt includes the end character + trailing space */
            size_t end = i;
            if (end < len && line[end] == ' ') end++;
            return end;
        }
    }

    return 0; /* No prompt detected */
}
