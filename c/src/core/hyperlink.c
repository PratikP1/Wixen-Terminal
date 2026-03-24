/* hyperlink.c — OSC 8 hyperlink store */
#include "wixen/core/hyperlink.h"
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

void wixen_hyperlinks_init(WixenHyperlinkStore *hs) {
    memset(hs, 0, sizeof(*hs));
    /* Index 0 = sentinel (no link) */
    hs->cap = 16;
    hs->links = calloc(hs->cap, sizeof(WixenHyperlink));
    hs->count = 1; /* Sentinel at index 0 */
}

void wixen_hyperlinks_free(WixenHyperlinkStore *hs) {
    for (size_t i = 0; i < hs->count; i++) {
        free(hs->links[i].uri);
        free(hs->links[i].id);
    }
    free(hs->links);
    memset(hs, 0, sizeof(*hs));
}

uint32_t wixen_hyperlinks_get_or_insert(WixenHyperlinkStore *hs,
                                         const char *uri, const char *id) {
    /* Check for existing link with same URI */
    for (size_t i = 1; i < hs->count; i++) {
        if (hs->links[i].uri && strcmp(hs->links[i].uri, uri) == 0) {
            return (uint32_t)i;
        }
    }

    /* Insert new */
    if (hs->count >= hs->cap) {
        size_t new_cap = hs->cap * 2;
        WixenHyperlink *new_arr = realloc(hs->links, new_cap * sizeof(WixenHyperlink));
        if (!new_arr) return 0;
        hs->links = new_arr;
        hs->cap = new_cap;
    }

    uint32_t idx = (uint32_t)hs->count;
    hs->links[idx].uri = dup_str(uri);
    hs->links[idx].id = dup_str(id);
    hs->count++;
    return idx;
}

void wixen_hyperlinks_open(WixenHyperlinkStore *hs, uint32_t link_id) {
    hs->active_id = link_id;
}

void wixen_hyperlinks_close(WixenHyperlinkStore *hs) {
    hs->active_id = 0;
}

const WixenHyperlink *wixen_hyperlinks_get(const WixenHyperlinkStore *hs, uint32_t link_id) {
    if (link_id == 0 || link_id >= hs->count) return NULL;
    return &hs->links[link_id];
}
