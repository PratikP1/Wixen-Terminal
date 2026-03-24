/* keybindings.c — Keybinding normalization, parsing, and lookup */
#include "wixen/config/keybindings.h"
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

static void to_lower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* --- Chord normalization --- */

char *wixen_chord_normalize(const char *chord) {
    if (!chord || !chord[0]) return dup_str("");

    char buf[256];
    size_t len = strlen(chord);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, chord, len);
    buf[len] = '\0';
    to_lower(buf);

    /* Parse into tokens by '+' */
    bool has_ctrl = false, has_shift = false, has_alt = false, has_win = false;
    char key[64] = {0};

    char *saveptr = NULL;
    char *token = strtok_s(buf, "+", &saveptr);
    while (token) {
        /* Trim whitespace */
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';

        if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            has_ctrl = true;
        } else if (strcmp(token, "shift") == 0) {
            has_shift = true;
        } else if (strcmp(token, "alt") == 0) {
            has_alt = true;
        } else if (strcmp(token, "win") == 0 || strcmp(token, "meta") == 0
                   || strcmp(token, "super") == 0 || strcmp(token, "cmd") == 0) {
            has_win = true;
        } else {
            /* Must be the key */
            size_t klen = strlen(token);
            if (klen < sizeof(key)) {
                memcpy(key, token, klen + 1);
            }
        }
        token = strtok_s(NULL, "+", &saveptr);
    }

    /* Build normalized string: modifiers in fixed order + key */
    char result[256] = {0};
    size_t pos = 0;
    if (has_ctrl) { memcpy(result + pos, "ctrl+", 5); pos += 5; }
    if (has_shift) { memcpy(result + pos, "shift+", 6); pos += 6; }
    if (has_alt) { memcpy(result + pos, "alt+", 4); pos += 4; }
    if (has_win) { memcpy(result + pos, "win+", 4); pos += 4; }
    size_t klen = strlen(key);
    if (klen > 0 && pos + klen < sizeof(result)) {
        memcpy(result + pos, key, klen);
        pos += klen;
    } else if (pos > 0) {
        pos--; /* Remove trailing '+' */
    }
    result[pos] = '\0';

    return dup_str(result);
}

/* --- Chord parsing --- */

bool wixen_chord_parse(const char *chord, WixenParsedChord *out) {
    memset(out, 0, sizeof(*out));
    char *norm = wixen_chord_normalize(chord);
    if (!norm || !norm[0]) { free(norm); return false; }

    char *saveptr = NULL;
    char *token = strtok_s(norm, "+", &saveptr);
    while (token) {
        if (strcmp(token, "ctrl") == 0) out->ctrl = true;
        else if (strcmp(token, "shift") == 0) out->shift = true;
        else if (strcmp(token, "alt") == 0) out->alt = true;
        else if (strcmp(token, "win") == 0) out->win = true;
        else {
            size_t len = strlen(token);
            if (len < sizeof(out->key)) memcpy(out->key, token, len + 1);
        }
        token = strtok_s(NULL, "+", &saveptr);
    }
    free(norm);
    return out->key[0] != '\0';
}

/* --- Keybinding map --- */

void wixen_keybindings_init(WixenKeybindingMap *km) {
    memset(km, 0, sizeof(*km));
}

void wixen_keybindings_free(WixenKeybindingMap *km) {
    for (size_t i = 0; i < km->count; i++) {
        free(km->bindings[i].chord);
        free(km->bindings[i].action);
        free(km->bindings[i].args);
    }
    free(km->bindings);
    memset(km, 0, sizeof(*km));
}

void wixen_keybindings_add(WixenKeybindingMap *km,
                            const char *chord, const char *action, const char *args) {
    if (km->count >= km->cap) {
        size_t new_cap = km->cap ? km->cap * 2 : 64;
        WixenKeybinding *new_arr = realloc(km->bindings, new_cap * sizeof(WixenKeybinding));
        if (!new_arr) return;
        km->bindings = new_arr;
        km->cap = new_cap;
    }

    char *norm = wixen_chord_normalize(chord);
    WixenKeybinding *b = &km->bindings[km->count++];
    b->chord = norm;
    b->action = dup_str(action);
    b->args = dup_str(args);
}

const char *wixen_keybindings_lookup(const WixenKeybindingMap *km, const char *chord) {
    char *norm = wixen_chord_normalize(chord);
    if (!norm) return NULL;

    for (size_t i = 0; i < km->count; i++) {
        if (strcmp(km->bindings[i].chord, norm) == 0) {
            free(norm);
            return km->bindings[i].action;
        }
    }
    free(norm);
    return NULL;
}

/* --- Default keybindings --- */

void wixen_keybindings_load_defaults(WixenKeybindingMap *km) {
    /* Tabs */
    wixen_keybindings_add(km, "ctrl+shift+t", "new_tab", NULL);
    wixen_keybindings_add(km, "ctrl+shift+w", "close_pane", NULL);
    wixen_keybindings_add(km, "ctrl+tab", "next_tab", NULL);
    wixen_keybindings_add(km, "ctrl+shift+tab", "prev_tab", NULL);
    wixen_keybindings_add(km, "ctrl+1", "select_tab_1", NULL);
    wixen_keybindings_add(km, "ctrl+2", "select_tab_2", NULL);
    wixen_keybindings_add(km, "ctrl+3", "select_tab_3", NULL);
    wixen_keybindings_add(km, "ctrl+4", "select_tab_4", NULL);
    wixen_keybindings_add(km, "ctrl+5", "select_tab_5", NULL);
    wixen_keybindings_add(km, "ctrl+6", "select_tab_6", NULL);
    wixen_keybindings_add(km, "ctrl+7", "select_tab_7", NULL);
    wixen_keybindings_add(km, "ctrl+8", "select_tab_8", NULL);
    wixen_keybindings_add(km, "ctrl+9", "select_tab_9", NULL);

    /* Panes */
    wixen_keybindings_add(km, "alt+shift+plus", "split_horizontal", NULL);
    wixen_keybindings_add(km, "alt+shift+minus", "split_vertical", NULL);
    wixen_keybindings_add(km, "alt+left", "focus_pane_left", NULL);
    wixen_keybindings_add(km, "alt+right", "focus_pane_right", NULL);
    wixen_keybindings_add(km, "alt+up", "focus_pane_up", NULL);
    wixen_keybindings_add(km, "alt+down", "focus_pane_down", NULL);

    /* Clipboard */
    wixen_keybindings_add(km, "ctrl+shift+c", "copy", NULL);
    wixen_keybindings_add(km, "ctrl+shift+v", "paste", NULL);

    /* Search */
    wixen_keybindings_add(km, "ctrl+shift+f", "find", NULL);

    /* View */
    wixen_keybindings_add(km, "ctrl+shift+p", "command_palette", NULL);
    wixen_keybindings_add(km, "ctrl+plus", "zoom_in", NULL);
    wixen_keybindings_add(km, "ctrl+minus", "zoom_out", NULL);
    wixen_keybindings_add(km, "ctrl+0", "zoom_reset", NULL);
    wixen_keybindings_add(km, "f11", "toggle_fullscreen", NULL);
    wixen_keybindings_add(km, "alt+enter", "toggle_fullscreen", NULL);

    /* Settings */
    wixen_keybindings_add(km, "ctrl+comma", "settings", NULL);

    /* Scroll */
    wixen_keybindings_add(km, "shift+pageup", "scroll_up_page", NULL);
    wixen_keybindings_add(km, "shift+pagedown", "scroll_down_page", NULL);
    wixen_keybindings_add(km, "ctrl+home", "scroll_to_top", NULL);
    wixen_keybindings_add(km, "ctrl+end", "scroll_to_bottom", NULL);
    wixen_keybindings_add(km, "ctrl+shift+up", "jump_prev_prompt", NULL);
    wixen_keybindings_add(km, "ctrl+shift+down", "jump_next_prompt", NULL);

    /* Window */
    wixen_keybindings_add(km, "ctrl+shift+n", "new_window", NULL);

    /* Terminal */
    wixen_keybindings_add(km, "ctrl+shift+l", "clear", NULL);
}
