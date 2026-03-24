/* xmalloc.c — Abort-on-failure memory allocation */
#include "wixen/core/xmalloc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void oom_abort(size_t size) {
    fprintf(stderr, "wixen: fatal: out of memory (requested %zu bytes)\n", size);
    abort();
}

void *wixen_xmalloc(size_t size) {
    if (size == 0) size = 1;
    void *p = malloc(size);
    if (!p) oom_abort(size);
    return p;
}

void *wixen_xcalloc(size_t count, size_t size) {
    if (count == 0) count = 1;
    if (size == 0) size = 1;
    void *p = calloc(count, size);
    if (!p) oom_abort(count * size);
    return p;
}

void *wixen_xrealloc(void *ptr, size_t size) {
    if (size == 0) size = 1;
    void *p = realloc(ptr, size);
    if (!p) oom_abort(size);
    return p;
}

char *wixen_xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = (char *)malloc(len + 1);
    if (!p) oom_abort(len + 1);
    memcpy(p, s, len + 1);
    return p;
}
