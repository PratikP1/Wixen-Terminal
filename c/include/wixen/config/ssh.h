/* ssh.h — SSH target parsing */
#ifndef WIXEN_CONFIG_SSH_H
#define WIXEN_CONFIG_SSH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *host;
    char *user;
    uint16_t port;
    char *key_path;
} WixenSshTarget;

/* Parse an SSH URL (ssh://user@host:port) */
bool wixen_ssh_parse_url(const char *url, WixenSshTarget *out);
void wixen_ssh_target_free(WixenSshTarget *target);

/* Build command line for ssh.exe */
bool wixen_ssh_to_command(const WixenSshTarget *target,
                           char *cmd_buf, size_t cmd_buf_size);

#endif /* WIXEN_CONFIG_SSH_H */
