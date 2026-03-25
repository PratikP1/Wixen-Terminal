/* url.c — URL detection */
#include "wixen/core/url.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *dup_str_n(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = '\0'; }
    return p;
}

static bool starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Detect a URL starting at position p. Returns end pointer or NULL. */
static const char *scan_url(const char *p) {
    /* Must start with a scheme */
    if (!starts_with(p, "http://") && !starts_with(p, "https://")
        && !starts_with(p, "ftp://") && !starts_with(p, "file://")
        && !starts_with(p, "ssh://") && !starts_with(p, "mailto:")) {
        return NULL;
    }

    /* Advance past scheme */
    const char *end = p;
    while (*end && !isspace((unsigned char)*end)) {
        end++;
    }

    /* Strip trailing punctuation */
    while (end > p) {
        char last = end[-1];
        if (last == '.' || last == ',' || last == ';' || last == ':'
            || last == '!' || last == '?' || last == ')' || last == ']'
            || last == '>') {
            end--;
        } else {
            break;
        }
    }

    return end > p ? end : NULL;
}

static size_t detect_urls_impl(const char *text, WixenUrlMatch **out_matches, bool safe_only) {
    if (!text || !out_matches) { if (out_matches) *out_matches = NULL; return 0; }

    WixenUrlMatch *matches = NULL;
    size_t count = 0, cap = 0;

    const char *p = text;
    while (*p) {
        const char *end = scan_url(p);
        if (end) {
            size_t url_len = (size_t)(end - p);
            char *url = dup_str_n(p, url_len);

            if (!safe_only || wixen_is_safe_url_scheme(url)) {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 8;
                    WixenUrlMatch *new_arr = realloc(matches, cap * sizeof(WixenUrlMatch));
                    if (!new_arr) { free(url); break; }
                    matches = new_arr;
                }
                matches[count].col_start = (size_t)(p - text);
                matches[count].col_end = (size_t)(end - text);
                matches[count].url = url;
                count++;
            } else {
                free(url);
            }
            p = end;
        } else {
            p++;
        }
    }

    *out_matches = matches;
    return count;
}

size_t wixen_detect_urls(const char *text, WixenUrlMatch **out_matches) {
    return detect_urls_impl(text, out_matches, false);
}

size_t wixen_detect_safe_urls(const char *text, WixenUrlMatch **out_matches) {
    return detect_urls_impl(text, out_matches, true);
}

bool wixen_is_safe_url_scheme(const char *url) {
    return starts_with(url, "http://") || starts_with(url, "https://")
        || starts_with(url, "ftp://");
}

char *wixen_url_at_col(const char *text, size_t col) {
    WixenUrlMatch *matches = NULL;
    size_t count = wixen_detect_urls(text, &matches);
    char *result = NULL;
    for (size_t i = 0; i < count; i++) {
        if (col >= matches[i].col_start && col < matches[i].col_end) {
            result = matches[i].url;
            matches[i].url = NULL; /* Don't free this one */
            break;
        }
    }
    wixen_url_matches_free(matches, count);
    return result;
}

void wixen_url_matches_free(WixenUrlMatch *matches, size_t count) {
    if (!matches) return;
    for (size_t i = 0; i < count; i++) {
        free(matches[i].url);
    }
    free(matches);
}
