/* error_detect.h — Output line classification (error/warning/info) */
#ifndef WIXEN_CORE_ERROR_DETECT_H
#define WIXEN_CORE_ERROR_DETECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WIXEN_LINE_NORMAL = 0,
    WIXEN_LINE_ERROR,
    WIXEN_LINE_WARNING,
    WIXEN_LINE_INFO,
} WixenOutputLineClass;

/* Classify a single output line */
WixenOutputLineClass wixen_classify_output_line(const char *line);

/* Count errors and warnings in an array of lines */
void wixen_count_errors_warnings(const char **lines, size_t count,
                                  size_t *out_errors, size_t *out_warnings);

/* Detect progress percentage (0-100) from a line. Returns -1 if none found. */
int wixen_detect_progress(const char *line);

#endif /* WIXEN_CORE_ERROR_DETECT_H */
