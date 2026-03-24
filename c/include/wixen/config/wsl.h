/* wsl.h — WSL distro detection and launch */
#ifndef WIXEN_CONFIG_WSL_H
#define WIXEN_CONFIG_WSL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *name;
    bool is_default;
} WixenWslDistro;

typedef struct {
    WixenWslDistro *distros;
    size_t count;
    size_t cap;
} WixenWslList;

void wixen_wsl_list_init(WixenWslList *list);
void wixen_wsl_list_free(WixenWslList *list);
void wixen_wsl_list_add(WixenWslList *list, const char *name, bool is_default);

/* Build command to launch a WSL distro */
bool wixen_wsl_to_command(const char *distro_name, char *cmd_buf, size_t cmd_buf_size);

#endif /* WIXEN_CONFIG_WSL_H */
