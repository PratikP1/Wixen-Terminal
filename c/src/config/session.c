/* session.c — Session persistence using JSON */
#include "wixen/config/session.h"
#include <cjson/cJSON.h>
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

void wixen_session_init(WixenSessionState *ss) {
    memset(ss, 0, sizeof(*ss));
}

void wixen_session_free(WixenSessionState *ss) {
    for (size_t i = 0; i < ss->tab_count; i++) {
        free(ss->tabs[i].title);
        free(ss->tabs[i].profile_name);
        free(ss->tabs[i].working_directory);
    }
    free(ss->tabs);
    memset(ss, 0, sizeof(*ss));
}

void wixen_session_add_tab(WixenSessionState *ss, const char *title,
                            const char *profile, const char *cwd) {
    size_t new_count = ss->tab_count + 1;
    WixenSavedTab *new_arr = realloc(ss->tabs, new_count * sizeof(WixenSavedTab));
    if (!new_arr) return;
    ss->tabs = new_arr;
    WixenSavedTab *tab = &ss->tabs[ss->tab_count];
    tab->title = dup_str(title);
    tab->profile_name = dup_str(profile);
    tab->working_directory = dup_str(cwd);
    ss->tab_count = new_count;
}

bool wixen_session_save(const WixenSessionState *ss, const char *path) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "active_tab", (double)ss->active_tab);

    cJSON *tabs_arr = cJSON_CreateArray();
    for (size_t i = 0; i < ss->tab_count; i++) {
        cJSON *tab = cJSON_CreateObject();
        cJSON_AddStringToObject(tab, "title", ss->tabs[i].title ? ss->tabs[i].title : "");
        cJSON_AddStringToObject(tab, "profile", ss->tabs[i].profile_name ? ss->tabs[i].profile_name : "");
        cJSON_AddStringToObject(tab, "cwd", ss->tabs[i].working_directory ? ss->tabs[i].working_directory : "");
        cJSON_AddItemToArray(tabs_arr, tab);
    }
    cJSON_AddItemToObject(root, "tabs", tabs_arr);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return false;

    FILE *f = fopen(path, "w");
    if (!f) { free(json); return false; }
    fputs(json, f);
    fclose(f);
    free(json);
    return true;
}

bool wixen_session_load(WixenSessionState *ss, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    wixen_session_init(ss);

    cJSON *active = cJSON_GetObjectItem(root, "active_tab");
    if (cJSON_IsNumber(active)) ss->active_tab = (size_t)active->valuedouble;

    cJSON *tabs_arr = cJSON_GetObjectItem(root, "tabs");
    if (cJSON_IsArray(tabs_arr)) {
        cJSON *tab;
        cJSON_ArrayForEach(tab, tabs_arr) {
            cJSON *title = cJSON_GetObjectItem(tab, "title");
            cJSON *profile = cJSON_GetObjectItem(tab, "profile");
            cJSON *cwd = cJSON_GetObjectItem(tab, "cwd");
            wixen_session_add_tab(ss,
                cJSON_IsString(title) ? title->valuestring : "",
                cJSON_IsString(profile) ? profile->valuestring : "",
                cJSON_IsString(cwd) ? cwd->valuestring : "");
        }
    }

    cJSON_Delete(root);
    return true;
}
