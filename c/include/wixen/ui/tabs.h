/* tabs.h — Tab management */
#ifndef WIXEN_UI_TABS_H
#define WIXEN_UI_TABS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t WixenTabId;
typedef uint64_t WixenPaneId;

typedef struct {
    uint8_t r, g, b;
} WixenTabColor;

typedef struct {
    WixenTabId id;
    char *title;
    WixenPaneId active_pane;
    bool has_bell;
    WixenTabColor *tab_color;    /* NULL if no custom color */
    int *exit_status;            /* NULL=running, pointer to exit code */
    char *profile_name;
} WixenTab;

typedef struct {
    WixenTab *tabs;
    size_t tab_count;
    size_t tab_cap;
    size_t active_idx;
    size_t prev_active_idx;
    uint64_t next_id;
} WixenTabManager;

/* Lifecycle */
void wixen_tabs_init(WixenTabManager *tm);
void wixen_tabs_free(WixenTabManager *tm);

/* Tab operations */
WixenTabId wixen_tabs_add(WixenTabManager *tm, const char *title, WixenPaneId pane);
bool wixen_tabs_close(WixenTabManager *tm, WixenTabId id);
bool wixen_tabs_switch(WixenTabManager *tm, WixenTabId id);
bool wixen_tabs_cycle(WixenTabManager *tm, bool forward);
void wixen_tabs_set_title(WixenTabManager *tm, WixenTabId id, const char *title);

/* Access */
const WixenTab *wixen_tabs_active(const WixenTabManager *tm);
const WixenTab *wixen_tabs_get(const WixenTabManager *tm, WixenTabId id);
size_t wixen_tabs_count(const WixenTabManager *tm);
const WixenTab *wixen_tabs_all(const WixenTabManager *tm, size_t *count);

/* Find tab index by ID. Returns SIZE_MAX if not found. */
size_t wixen_tabs_index_of(const WixenTabManager *tm, WixenTabId id);

#endif /* WIXEN_UI_TABS_H */
