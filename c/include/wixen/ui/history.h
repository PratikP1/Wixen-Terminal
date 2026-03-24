/* history.h — Command history tracking */
#ifndef WIXEN_UI_HISTORY_H
#define WIXEN_UI_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *command;
    int exit_code;
    bool has_exit_code;
    char *cwd;
    uint64_t block_id;
} WixenHistoryEntry;

typedef struct {
    WixenHistoryEntry *entries;  /* Newest first */
    size_t count;
    size_t cap;
} WixenCommandHistory;

void wixen_history_init(WixenCommandHistory *h);
void wixen_history_free(WixenCommandHistory *h);
void wixen_history_push(WixenCommandHistory *h, const char *command,
                         int exit_code, bool has_exit, const char *cwd, uint64_t block_id);
void wixen_history_clear(WixenCommandHistory *h);
size_t wixen_history_count(const WixenCommandHistory *h);
const WixenHistoryEntry *wixen_history_get(const WixenCommandHistory *h, size_t index);

/* Search — returns indices of matching entries. Caller must free the array. */
size_t wixen_history_search(const WixenCommandHistory *h, const char *query,
                             size_t **out_indices);

/* Most recent N entries (returns pointer to internal array, do not free) */
const WixenHistoryEntry *wixen_history_recent(const WixenCommandHistory *h,
                                               size_t n, size_t *out_count);

#endif /* WIXEN_UI_HISTORY_H */
