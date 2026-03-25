/* config.h — TOML configuration parser and structure */
#ifndef WIXEN_CONFIG_H
#define WIXEN_CONFIG_H

#include "wixen/config/keybindings.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name;
    char *program;
    char **args;
    size_t arg_count;
    char *working_directory;
    bool is_default;
} WixenProfile;

typedef struct {
    char *family;
    float size;
    float line_height;
    bool ligatures;
} WixenFontConfig;

typedef struct {
    uint32_t width;
    uint32_t height;
    char *theme;
    float opacity;
} WixenWindowConfig;

typedef struct {
    char *cursor_style;    /* "block", "underline", "bar" */
    bool cursor_blink;
    char *bell_style;      /* "mute", "visual", "system" */
} WixenTerminalConfig;

typedef struct {
    char *verbosity;       /* "all", "basic", "none" */
    uint32_t output_debounce_ms;
} WixenAccessibilityConfig;

typedef struct {
    bool copy_on_selection;
    bool paste_with_ctrl_v;
} WixenBehaviorConfig;

typedef struct {
    WixenFontConfig font;
    WixenWindowConfig window;
    WixenTerminalConfig terminal;
    WixenAccessibilityConfig accessibility;
    WixenBehaviorConfig behavior;
    WixenProfile *profiles;
    size_t profile_count;
    WixenKeybindingMap keybindings;
} WixenConfig;

/* Lifecycle */
void wixen_config_init_defaults(WixenConfig *cfg);
void wixen_config_free(WixenConfig *cfg);

/* Load from TOML file. Returns false on error. */
bool wixen_config_load(WixenConfig *cfg, const char *path);

/* Save to TOML file */
bool wixen_config_save(const WixenConfig *cfg, const char *path);

/* Get the default profile (first with is_default=true, or first) */
const WixenProfile *wixen_config_default_profile(const WixenConfig *cfg);

/* Get default config file path (~/.config/wixen/config.toml or %APPDATA%/wixen) */
void wixen_config_default_path(char *buf, size_t buf_size);

/* Reduced motion preference */
typedef enum {
    WIXEN_REDUCED_MOTION_SYSTEM,  /* Follow OS preference */
    WIXEN_REDUCED_MOTION_ALWAYS,  /* Always reduce */
    WIXEN_REDUCED_MOTION_NEVER    /* Never reduce */
} WixenReducedMotion;

/* Check if motion should be reduced based on config + system preference */
bool wixen_should_reduce_motion(WixenReducedMotion config_pref, bool system_prefers_reduced);

#endif /* WIXEN_CONFIG_H */
