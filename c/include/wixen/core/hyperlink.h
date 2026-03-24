/* hyperlink.h — OSC 8 hyperlink store */
#ifndef WIXEN_CORE_HYPERLINK_H
#define WIXEN_CORE_HYPERLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *uri;
    char *id;        /* Optional link ID for grouping */
} WixenHyperlink;

typedef struct {
    WixenHyperlink *links;  /* Index 0 is sentinel (no link) */
    size_t count;
    size_t cap;
    uint32_t active_id;     /* Currently open link (0 = none) */
} WixenHyperlinkStore;

void wixen_hyperlinks_init(WixenHyperlinkStore *hs);
void wixen_hyperlinks_free(WixenHyperlinkStore *hs);

/* Get or insert a hyperlink. Returns the link ID (>= 1). */
uint32_t wixen_hyperlinks_get_or_insert(WixenHyperlinkStore *hs,
                                         const char *uri, const char *id);

/* Open a link (set active) */
void wixen_hyperlinks_open(WixenHyperlinkStore *hs, uint32_t link_id);

/* Close current link */
void wixen_hyperlinks_close(WixenHyperlinkStore *hs);

/* Get a link by ID. Returns NULL if not found. */
const WixenHyperlink *wixen_hyperlinks_get(const WixenHyperlinkStore *hs, uint32_t link_id);

#endif /* WIXEN_CORE_HYPERLINK_H */
