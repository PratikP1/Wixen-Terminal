/* history.c — Command history */
#include "wixen/ui/history.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

static void entry_free(WixenHistoryEntry *e) {
    free(e->command);
    free(e->cwd);
    e->command = NULL;
    e->cwd = NULL;
}

void wixen_history_init(WixenCommandHistory *h) {
    memset(h, 0, sizeof(*h));
}

void wixen_history_free(WixenCommandHistory *h) {
    for (size_t i = 0; i < h->count; i++) entry_free(&h->entries[i]);
    free(h->entries);
    memset(h, 0, sizeof(*h));
}

void wixen_history_push(WixenCommandHistory *h, const char *command,
                         int exit_code, bool has_exit, const char *cwd, uint64_t block_id) {
    if (!command || !command[0]) return;

    /* Deduplicate consecutive duplicates */
    if (h->count > 0 && h->entries[0].command && strcmp(h->entries[0].command, command) == 0) {
        /* Update exit code of existing entry */
        h->entries[0].exit_code = exit_code;
        h->entries[0].has_exit_code = has_exit;
        return;
    }

    if (h->count >= h->cap) {
        size_t new_cap = h->cap ? h->cap * 2 : 64;
        WixenHistoryEntry *new_arr = realloc(h->entries, new_cap * sizeof(WixenHistoryEntry));
        if (!new_arr) return;
        h->entries = new_arr;
        h->cap = new_cap;
    }

    /* Shift existing entries down (newest first) */
    if (h->count > 0) {
        memmove(&h->entries[1], &h->entries[0], h->count * sizeof(WixenHistoryEntry));
    }

    h->entries[0].command = dup_str(command);
    h->entries[0].exit_code = exit_code;
    h->entries[0].has_exit_code = has_exit;
    h->entries[0].cwd = dup_str(cwd);
    h->entries[0].block_id = block_id;
    h->count++;
}

void wixen_history_clear(WixenCommandHistory *h) {
    for (size_t i = 0; i < h->count; i++) entry_free(&h->entries[i]);
    h->count = 0;
}

size_t wixen_history_count(const WixenCommandHistory *h) {
    return h->count;
}

const WixenHistoryEntry *wixen_history_get(const WixenCommandHistory *h, size_t index) {
    if (index >= h->count) return NULL;
    return &h->entries[index];
}

/* Case-insensitive substring */
static bool ci_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
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

size_t wixen_history_search(const WixenCommandHistory *h, const char *query,
                             size_t **out_indices) {
    if (!query || !query[0] || !out_indices) {
        if (out_indices) *out_indices = NULL;
        return 0;
    }

    size_t *indices = NULL;
    size_t count = 0, cap = 0;

    for (size_t i = 0; i < h->count; i++) {
        if (ci_contains(h->entries[i].command, query)) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 16;
                size_t *new_arr = realloc(indices, cap * sizeof(size_t));
                if (!new_arr) break;
                indices = new_arr;
            }
            indices[count++] = i;
        }
    }

    *out_indices = indices;
    return count;
}

const WixenHistoryEntry *wixen_history_recent(const WixenCommandHistory *h,
                                               size_t n, size_t *out_count) {
    size_t actual = n < h->count ? n : h->count;
    if (out_count) *out_count = actual;
    return h->entries; /* Already newest-first */
}
