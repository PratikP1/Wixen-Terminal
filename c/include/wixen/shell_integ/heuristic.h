/* heuristic.h — Heuristic prompt detection (fallback when OSC 133 unavailable) */
#ifndef WIXEN_SHELL_INTEG_HEURISTIC_H
#define WIXEN_SHELL_INTEG_HEURISTIC_H

#include <stdbool.h>
#include <stddef.h>

/* Check if a line looks like a shell prompt */
bool wixen_is_prompt_line(const char *line);

/* Extract the prompt portion (everything before the last > $ # etc.) */
size_t wixen_prompt_end_col(const char *line);

#endif /* WIXEN_SHELL_INTEG_HEURISTIC_H */
