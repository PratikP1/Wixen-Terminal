/* keybindings.h — Keybinding normalization, parsing, lookup */
#ifndef WIXEN_CONFIG_KEYBINDINGS_H
#define WIXEN_CONFIG_KEYBINDINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *chord;       /* Normalized: "ctrl+shift+t" */
    char *action;      /* Action ID: "new_tab", "copy", etc. */
    char *args;        /* Optional argument (NULL if none) */
} WixenKeybinding;

typedef struct {
    bool ctrl;
    bool shift;
    bool alt;
    bool win;
    char key[32];      /* Key name: "t", "f1", "tab", "escape", etc. */
} WixenParsedChord;

typedef struct {
    WixenKeybinding *bindings;
    size_t count;
    size_t cap;
} WixenKeybindingMap;

/* Lifecycle */
void wixen_keybindings_init(WixenKeybindingMap *km);
void wixen_keybindings_free(WixenKeybindingMap *km);

/* Add a binding. chord is normalized before storage. */
void wixen_keybindings_add(WixenKeybindingMap *km,
                            const char *chord, const char *action, const char *args);

/* Look up action by chord. Returns NULL if not found. */
const char *wixen_keybindings_lookup(const WixenKeybindingMap *km, const char *chord);

/* Normalize a chord string: lowercase, sort modifiers, deduplicate.
   Returns heap-allocated string. Caller must free. */
char *wixen_chord_normalize(const char *chord);

/* Parse a chord into components */
bool wixen_chord_parse(const char *chord, WixenParsedChord *out);

/* Remove a keybinding by chord. Returns true if found and removed. */
bool wixen_keybindings_remove(WixenKeybindingMap *km, const char *chord);

/* Load default keybindings (Windows Terminal-compatible) */
void wixen_keybindings_load_defaults(WixenKeybindingMap *km);

#endif /* WIXEN_CONFIG_KEYBINDINGS_H */
