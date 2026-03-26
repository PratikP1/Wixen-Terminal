/* path_util.c — Safe path manipulation */
#include "wixen/core/path_util.h"
#include <string.h>

bool wixen_session_path_from_config(const char *config_path,
                                     char *out, size_t out_size) {
    if (!config_path || !config_path[0] || !out || out_size == 0)
        return false;

    size_t len = strlen(config_path);

    /* Find last dot (extension) */
    const char *dot = NULL;
    const char *sep = NULL;
    for (size_t i = len; i > 0; i--) {
        char c = config_path[i - 1];
        if (c == '.' && !dot) dot = config_path + i - 1;
        if (c == '\\' || c == '/') { sep = config_path + i - 1; break; }
    }
    /* Dot must be after last separator */
    if (dot && sep && dot < sep) dot = NULL;

    /* Compute base length (without extension) */
    size_t base_len = dot ? (size_t)(dot - config_path) : len;
    const char *suffix = ".session.json";
    size_t suffix_len = strlen(suffix);
    size_t total = base_len + suffix_len;

    if (total + 1 > out_size)
        return false;

    memcpy(out, config_path, base_len);
    memcpy(out + base_len, suffix, suffix_len);
    out[total] = '\0';
    return true;
}
