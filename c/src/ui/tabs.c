/* tabs.c — Tab management */
#include "wixen/ui/tabs.h"
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

static void tab_free(WixenTab *tab) {
    free(tab->title);
    free(tab->tab_color);
    free(tab->exit_status);
    free(tab->profile_name);
    tab->title = NULL;
    tab->tab_color = NULL;
    tab->exit_status = NULL;
    tab->profile_name = NULL;
}

void wixen_tabs_init(WixenTabManager *tm) {
    memset(tm, 0, sizeof(*tm));
}

void wixen_tabs_free(WixenTabManager *tm) {
    for (size_t i = 0; i < tm->tab_count; i++) {
        tab_free(&tm->tabs[i]);
    }
    free(tm->tabs);
    memset(tm, 0, sizeof(*tm));
}

WixenTabId wixen_tabs_add(WixenTabManager *tm, const char *title, WixenPaneId pane) {
    if (tm->tab_count >= tm->tab_cap) {
        size_t new_cap = tm->tab_cap ? tm->tab_cap * 2 : 8;
        WixenTab *new_arr = realloc(tm->tabs, new_cap * sizeof(WixenTab));
        if (!new_arr) return 0;
        tm->tabs = new_arr;
        tm->tab_cap = new_cap;
    }

    WixenTabId id = ++tm->next_id;
    WixenTab *tab = &tm->tabs[tm->tab_count];
    memset(tab, 0, sizeof(*tab));
    tab->id = id;
    tab->title = dup_str(title ? title : "Shell");
    tab->active_pane = pane;

    tm->tab_count++;
    tm->active_idx = tm->tab_count - 1;
    return id;
}

size_t wixen_tabs_index_of(const WixenTabManager *tm, WixenTabId id) {
    for (size_t i = 0; i < tm->tab_count; i++) {
        if (tm->tabs[i].id == id) return i;
    }
    return (size_t)-1;
}

bool wixen_tabs_close(WixenTabManager *tm, WixenTabId id) {
    size_t idx = wixen_tabs_index_of(tm, id);
    if (idx == (size_t)-1) return false;
    if (tm->tab_count <= 1) return false; /* Can't close last tab */

    tab_free(&tm->tabs[idx]);

    /* Shift remaining tabs */
    size_t remaining = tm->tab_count - idx - 1;
    if (remaining > 0) {
        memmove(&tm->tabs[idx], &tm->tabs[idx + 1], remaining * sizeof(WixenTab));
    }
    tm->tab_count--;

    /* Adjust active index */
    if (tm->active_idx >= tm->tab_count) {
        tm->active_idx = tm->tab_count - 1;
    } else if (tm->active_idx > idx) {
        tm->active_idx--;
    }

    return true;
}

bool wixen_tabs_switch(WixenTabManager *tm, WixenTabId id) {
    size_t idx = wixen_tabs_index_of(tm, id);
    if (idx == (size_t)-1) return false;
    tm->prev_active_idx = tm->active_idx;
    tm->active_idx = idx;
    return true;
}

bool wixen_tabs_cycle(WixenTabManager *tm, bool forward) {
    if (tm->tab_count <= 1) return false;
    tm->prev_active_idx = tm->active_idx;
    if (forward) {
        tm->active_idx = (tm->active_idx + 1) % tm->tab_count;
    } else {
        tm->active_idx = (tm->active_idx + tm->tab_count - 1) % tm->tab_count;
    }
    return true;
}

void wixen_tabs_set_title(WixenTabManager *tm, WixenTabId id, const char *title) {
    size_t idx = wixen_tabs_index_of(tm, id);
    if (idx == (size_t)-1) return;
    free(tm->tabs[idx].title);
    tm->tabs[idx].title = dup_str(title);
}

const WixenTab *wixen_tabs_active(const WixenTabManager *tm) {
    if (tm->tab_count == 0) return NULL;
    return &tm->tabs[tm->active_idx];
}

const WixenTab *wixen_tabs_get(const WixenTabManager *tm, WixenTabId id) {
    size_t idx = wixen_tabs_index_of(tm, id);
    if (idx == (size_t)-1) return NULL;
    return &tm->tabs[idx];
}

size_t wixen_tabs_count(const WixenTabManager *tm) {
    return tm->tab_count;
}

const WixenTab *wixen_tabs_all(const WixenTabManager *tm, size_t *count) {
    if (count) *count = tm->tab_count;
    return tm->tabs;
}
