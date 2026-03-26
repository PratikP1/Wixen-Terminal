/* path_util.h — Safe path manipulation utilities */
#ifndef WIXEN_CORE_PATH_UTIL_H
#define WIXEN_CORE_PATH_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* Replace the extension of a config path with ".session.json".
 * If the input has no extension, appends ".session.json".
 * Returns false if input is NULL/empty or output buffer is too small.
 * Output is always null-terminated on success. */
bool wixen_session_path_from_config(const char *config_path,
                                     char *out, size_t out_size);

#endif /* WIXEN_CORE_PATH_UTIL_H */
