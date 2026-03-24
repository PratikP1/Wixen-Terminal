/* wsl.c — WSL distro management */
#include "wixen/config/wsl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = malloc(len + 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

void wixen_wsl_list_init(WixenWslList *list) {
    memset(list, 0, sizeof(*list));
}

void wixen_wsl_list_free(WixenWslList *list) {
    for (size_t i = 0; i < list->count; i++) free(list->distros[i].name);
    free(list->distros);
    memset(list, 0, sizeof(*list));
}

void wixen_wsl_list_add(WixenWslList *list, const char *name, bool is_default) {
    if (list->count >= list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 8;
        WixenWslDistro *arr = realloc(list->distros, new_cap * sizeof(WixenWslDistro));
        if (!arr) return;
        list->distros = arr;
        list->cap = new_cap;
    }
    list->distros[list->count].name = dup_str(name);
    list->distros[list->count].is_default = is_default;
    list->count++;
}

bool wixen_wsl_to_command(const char *distro_name, char *cmd_buf, size_t cmd_buf_size) {
    if (!distro_name || !distro_name[0])
        return snprintf(cmd_buf, cmd_buf_size, "wsl.exe") > 0;
    return snprintf(cmd_buf, cmd_buf_size, "wsl.exe -d %s", distro_name) > 0;
}
