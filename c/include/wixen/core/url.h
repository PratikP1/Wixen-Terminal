/* url.h — URL detection in terminal output */
#ifndef WIXEN_CORE_URL_H
#define WIXEN_CORE_URL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t col_start;
    size_t col_end;   /* Exclusive */
    char *url;        /* Heap-allocated, caller must free */
} WixenUrlMatch;

/* Detect URLs in text. Returns match count. Caller must free each url string
   and the matches array itself. */
size_t wixen_detect_urls(const char *text, WixenUrlMatch **out_matches);

/* Detect only safe URLs (http, https, ftp) */
size_t wixen_detect_safe_urls(const char *text, WixenUrlMatch **out_matches);

/* Check if a URL scheme is safe */
bool wixen_is_safe_url_scheme(const char *url);

/* Get URL at a specific column. Returns heap-allocated string or NULL. */
char *wixen_url_at_col(const char *text, size_t col);

/* Free a matches array */
void wixen_url_matches_free(WixenUrlMatch *matches, size_t count);

#endif /* WIXEN_CORE_URL_H */
