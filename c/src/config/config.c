/* config.c — Configuration loading with minimal TOML parser */
#include "wixen/config/config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

/* --- Defaults --- */

void wixen_config_init_defaults(WixenConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    cfg->font.family = dup_str("Cascadia Mono");
    cfg->font.size = 14.0f;
    cfg->font.line_height = 1.2f;
    cfg->font.ligatures = true;

    cfg->window.width = 1200;
    cfg->window.height = 800;
    cfg->window.theme = dup_str("default");
    cfg->window.opacity = 1.0f;

    cfg->terminal.cursor_style = dup_str("block");
    cfg->terminal.cursor_blink = true;
    cfg->terminal.bell_style = dup_str("visual");

    cfg->accessibility.verbosity = dup_str("all");
    cfg->accessibility.output_debounce_ms = 100;

    cfg->behavior.copy_on_selection = false;
    cfg->behavior.paste_with_ctrl_v = true;

    wixen_keybindings_init(&cfg->keybindings);
    wixen_keybindings_load_defaults(&cfg->keybindings);
}

void wixen_config_free(WixenConfig *cfg) {
    free(cfg->font.family);
    free(cfg->window.theme);
    free(cfg->terminal.cursor_style);
    free(cfg->terminal.bell_style);
    free(cfg->accessibility.verbosity);

    for (size_t i = 0; i < cfg->profile_count; i++) {
        free(cfg->profiles[i].name);
        free(cfg->profiles[i].program);
        for (size_t j = 0; j < cfg->profiles[i].arg_count; j++)
            free(cfg->profiles[i].args[j]);
        free(cfg->profiles[i].args);
        free(cfg->profiles[i].working_directory);
    }
    free(cfg->profiles);

    wixen_keybindings_free(&cfg->keybindings);
    memset(cfg, 0, sizeof(*cfg));
}

/* --- Minimal TOML parser --- */

typedef enum {
    TOML_NONE,
    TOML_FONT,
    TOML_WINDOW,
    TOML_TERMINAL,
    TOML_ACCESSIBILITY,
    TOML_BEHAVIOR,
    TOML_PROFILES,
    TOML_KEYBINDINGS,
} TomlSection;

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        *end-- = '\0';
    return s;
}

static char *unquote(char *s) {
    s = trim(s);
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        s[len - 1] = '\0';
        return s + 1;
    }
    return s;
}

static void add_profile(WixenConfig *cfg) {
    size_t new_count = cfg->profile_count + 1;
    WixenProfile *new_arr = realloc(cfg->profiles, new_count * sizeof(WixenProfile));
    if (!new_arr) return;
    cfg->profiles = new_arr;
    memset(&cfg->profiles[cfg->profile_count], 0, sizeof(WixenProfile));
    cfg->profile_count = new_count;
}

bool wixen_config_load(WixenConfig *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    wixen_config_init_defaults(cfg);

    char line[1024];
    TomlSection section = TOML_NONE;

    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (s[0] == '#' || s[0] == '\0') continue;

        /* Section headers */
        if (s[0] == '[') {
            if (strncmp(s, "[[profiles]]", 12) == 0) {
                section = TOML_PROFILES;
                add_profile(cfg);
            } else if (strncmp(s, "[[keybindings]]", 15) == 0) {
                section = TOML_KEYBINDINGS;
            } else if (strncmp(s, "[font]", 6) == 0) {
                section = TOML_FONT;
            } else if (strncmp(s, "[window]", 8) == 0) {
                section = TOML_WINDOW;
            } else if (strncmp(s, "[terminal]", 10) == 0) {
                section = TOML_TERMINAL;
            } else if (strncmp(s, "[accessibility]", 15) == 0) {
                section = TOML_ACCESSIBILITY;
            } else if (strncmp(s, "[behavior]", 10) == 0) {
                section = TOML_BEHAVIOR;
            }
            continue;
        }

        /* Key = value */
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        switch (section) {
        case TOML_FONT:
            if (strcmp(key, "family") == 0) {
                free(cfg->font.family);
                cfg->font.family = dup_str(unquote(val));
            } else if (strcmp(key, "size") == 0) {
                cfg->font.size = (float)atof(val);
            } else if (strcmp(key, "line_height") == 0) {
                cfg->font.line_height = (float)atof(val);
            } else if (strcmp(key, "ligatures") == 0) {
                cfg->font.ligatures = strcmp(val, "true") == 0;
            }
            break;

        case TOML_WINDOW:
            if (strcmp(key, "width") == 0) cfg->window.width = (uint32_t)atoi(val);
            else if (strcmp(key, "height") == 0) cfg->window.height = (uint32_t)atoi(val);
            else if (strcmp(key, "theme") == 0) {
                free(cfg->window.theme);
                cfg->window.theme = dup_str(unquote(val));
            } else if (strcmp(key, "opacity") == 0) {
                cfg->window.opacity = (float)atof(val);
            }
            break;

        case TOML_TERMINAL:
            if (strcmp(key, "cursor_style") == 0) {
                free(cfg->terminal.cursor_style);
                cfg->terminal.cursor_style = dup_str(unquote(val));
            } else if (strcmp(key, "cursor_blink") == 0) {
                cfg->terminal.cursor_blink = strcmp(val, "true") == 0;
            } else if (strcmp(key, "bell_style") == 0) {
                free(cfg->terminal.bell_style);
                cfg->terminal.bell_style = dup_str(unquote(val));
            }
            break;

        case TOML_ACCESSIBILITY:
            if (strcmp(key, "verbosity") == 0 || strcmp(key, "screen_reader_verbosity") == 0) {
                free(cfg->accessibility.verbosity);
                cfg->accessibility.verbosity = dup_str(unquote(val));
            } else if (strcmp(key, "output_debounce_ms") == 0) {
                cfg->accessibility.output_debounce_ms = (uint32_t)atoi(val);
            }
            break;

        case TOML_BEHAVIOR:
            if (strcmp(key, "copy_on_selection") == 0)
                cfg->behavior.copy_on_selection = strcmp(val, "true") == 0;
            else if (strcmp(key, "paste_with_ctrl_v") == 0)
                cfg->behavior.paste_with_ctrl_v = strcmp(val, "true") == 0;
            break;

        case TOML_PROFILES:
            if (cfg->profile_count > 0) {
                WixenProfile *p = &cfg->profiles[cfg->profile_count - 1];
                if (strcmp(key, "name") == 0) {
                    free(p->name); p->name = dup_str(unquote(val));
                } else if (strcmp(key, "program") == 0) {
                    free(p->program); p->program = dup_str(unquote(val));
                } else if (strcmp(key, "is_default") == 0) {
                    p->is_default = strcmp(val, "true") == 0;
                } else if (strcmp(key, "working_directory") == 0) {
                    free(p->working_directory);
                    p->working_directory = dup_str(unquote(val));
                }
            }
            break;

        case TOML_KEYBINDINGS:
            /* Format: chord = "action" or chord = { action = "name", args = "..." } */
            if (eq) {
                const char *chord = key;
                const char *action_str = unquote(val);
                if (chord[0] && action_str[0]) {
                    wixen_keybindings_add(&cfg->keybindings, chord, action_str, NULL);
                }
            }
            break;

        default:
            break;
        }
    }

    fclose(f);
    return true;
}

bool wixen_config_save(const WixenConfig *cfg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "[font]\n");
    fprintf(f, "family = \"%s\"\n", cfg->font.family ? cfg->font.family : "Cascadia Mono");
    fprintf(f, "size = %.1f\n", cfg->font.size);
    fprintf(f, "line_height = %.1f\n", cfg->font.line_height);
    fprintf(f, "ligatures = %s\n\n", cfg->font.ligatures ? "true" : "false");

    fprintf(f, "[window]\n");
    fprintf(f, "width = %u\n", cfg->window.width);
    fprintf(f, "height = %u\n", cfg->window.height);
    if (cfg->window.theme) fprintf(f, "theme = \"%s\"\n", cfg->window.theme);
    fprintf(f, "opacity = %.2f\n\n", cfg->window.opacity);

    fprintf(f, "[terminal]\n");
    fprintf(f, "cursor_style = \"%s\"\n", cfg->terminal.cursor_style ? cfg->terminal.cursor_style : "block");
    fprintf(f, "cursor_blink = %s\n", cfg->terminal.cursor_blink ? "true" : "false");
    fprintf(f, "bell_style = \"%s\"\n\n", cfg->terminal.bell_style ? cfg->terminal.bell_style : "visual");

    fprintf(f, "[accessibility]\n");
    fprintf(f, "verbosity = \"%s\"\n", cfg->accessibility.verbosity ? cfg->accessibility.verbosity : "all");
    fprintf(f, "output_debounce_ms = %u\n\n", cfg->accessibility.output_debounce_ms);

    fprintf(f, "[behavior]\n");
    fprintf(f, "copy_on_selection = %s\n", cfg->behavior.copy_on_selection ? "true" : "false");
    fprintf(f, "paste_with_ctrl_v = %s\n\n", cfg->behavior.paste_with_ctrl_v ? "true" : "false");

    for (size_t i = 0; i < cfg->profile_count; i++) {
        fprintf(f, "[[profiles]]\n");
        fprintf(f, "name = \"%s\"\n", cfg->profiles[i].name ? cfg->profiles[i].name : "Shell");
        fprintf(f, "program = \"%s\"\n", cfg->profiles[i].program ? cfg->profiles[i].program : "");
        if (cfg->profiles[i].is_default) fprintf(f, "is_default = true\n");
        fprintf(f, "\n");
    }

    fclose(f);
    return true;
}

const WixenProfile *wixen_config_default_profile(const WixenConfig *cfg) {
    for (size_t i = 0; i < cfg->profile_count; i++) {
        if (cfg->profiles[i].is_default) return &cfg->profiles[i];
    }
    if (cfg->profile_count > 0) return &cfg->profiles[0];
    return NULL;
}

void wixen_config_default_path(char *buf, size_t buf_size) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata) {
        snprintf(buf, buf_size, "%s\\wixen\\config.toml", appdata);
    } else {
        snprintf(buf, buf_size, "config.toml");
    }
#else
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buf, buf_size, "%s/.config/wixen/config.toml", home);
    } else {
        snprintf(buf, buf_size, "config.toml");
    }
#endif
}

bool wixen_should_reduce_motion(WixenReducedMotion config_pref, bool system_prefers_reduced) {
    switch (config_pref) {
    case WIXEN_REDUCED_MOTION_ALWAYS: return true;
    case WIXEN_REDUCED_MOTION_NEVER:  return false;
    case WIXEN_REDUCED_MOTION_SYSTEM: return system_prefers_reduced;
    default: return system_prefers_reduced;
    }
}
