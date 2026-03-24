/* xmalloc.h — Abort-on-failure memory allocation
 *
 * Terminal emulators should fail fast on OOM rather than limp along
 * with NULL pointers causing crashes later.
 */
#ifndef WIXEN_CORE_XMALLOC_H
#define WIXEN_CORE_XMALLOC_H

#include <stddef.h>

/* Allocate or abort. Never returns NULL. */
void *wixen_xmalloc(size_t size);
void *wixen_xcalloc(size_t count, size_t size);
void *wixen_xrealloc(void *ptr, size_t size);
char *wixen_xstrdup(const char *s);

#endif /* WIXEN_CORE_XMALLOC_H */
