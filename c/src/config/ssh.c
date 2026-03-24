/* ssh.c — SSH URL parsing */
#include "wixen/config/ssh.h"
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

bool wixen_ssh_parse_url(const char *url, WixenSshTarget *out) {
    memset(out, 0, sizeof(*out));
    out->port = 22;
    if (!url) return false;

    const char *p = url;
    /* Skip ssh:// prefix */
    if (strncmp(p, "ssh://", 6) == 0) p += 6;

    /* user@host:port */
    const char *at = strchr(p, '@');
    const char *colon;

    if (at) {
        out->user = malloc((size_t)(at - p) + 1);
        if (out->user) { memcpy(out->user, p, (size_t)(at - p)); out->user[at - p] = '\0'; }
        p = at + 1;
    }

    colon = strchr(p, ':');
    if (colon) {
        out->host = malloc((size_t)(colon - p) + 1);
        if (out->host) { memcpy(out->host, p, (size_t)(colon - p)); out->host[colon - p] = '\0'; }
        out->port = (uint16_t)atoi(colon + 1);
    } else {
        out->host = dup_str(p);
    }

    return out->host && out->host[0];
}

void wixen_ssh_target_free(WixenSshTarget *target) {
    free(target->host);
    free(target->user);
    free(target->key_path);
    memset(target, 0, sizeof(*target));
}

bool wixen_ssh_to_command(const WixenSshTarget *target,
                           char *cmd_buf, size_t cmd_buf_size) {
    if (!target || !target->host) return false;

    int written;
    if (target->user && target->port != 22) {
        written = snprintf(cmd_buf, cmd_buf_size, "ssh -p %d %s@%s",
                            target->port, target->user, target->host);
    } else if (target->user) {
        written = snprintf(cmd_buf, cmd_buf_size, "ssh %s@%s",
                            target->user, target->host);
    } else if (target->port != 22) {
        written = snprintf(cmd_buf, cmd_buf_size, "ssh -p %d %s",
                            target->port, target->host);
    } else {
        written = snprintf(cmd_buf, cmd_buf_size, "ssh %s", target->host);
    }
    return written > 0 && (size_t)written < cmd_buf_size;
}
