/* ipc.c — Named pipe IPC for multi-window */
#ifdef _WIN32

#include "wixen/ipc/ipc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* --- Message serialization --- */
/* Wire format: [4-byte magic][4-byte type][4-byte window_id][4-byte profile_len][profile][4-byte cwd_len][cwd] */
#define IPC_MAGIC 0x57495843  /* "WIXC" */

bool wixen_ipc_msg_serialize(const WixenIpcMessage *msg, uint8_t **out_buf, size_t *out_len) {
    size_t prof_len = (msg->new_tab.profile) ? strlen(msg->new_tab.profile) : 0;
    size_t cwd_len = (msg->new_tab.cwd) ? strlen(msg->new_tab.cwd) : 0;
    size_t total = 4 + 4 + 4 + 4 + prof_len + 4 + cwd_len;

    /* Enforce max message size */
    if (total > WIXEN_IPC_MAX_MESSAGE_SIZE) return false;

    uint8_t *buf = malloc(total);
    if (!buf) return false;
    size_t pos = 0;
    /* Magic */
    memcpy(buf + pos, &(uint32_t){IPC_MAGIC}, 4); pos += 4;
    /* Type */
    uint32_t type = (uint32_t)msg->type;
    memcpy(buf + pos, &type, 4); pos += 4;
    /* Window ID */
    memcpy(buf + pos, &msg->window_id, 4); pos += 4;
    /* Profile string */
    uint32_t plen = (uint32_t)prof_len;
    memcpy(buf + pos, &plen, 4); pos += 4;
    if (prof_len > 0) { memcpy(buf + pos, msg->new_tab.profile, prof_len); pos += prof_len; }
    /* CWD string */
    uint32_t clen = (uint32_t)cwd_len;
    memcpy(buf + pos, &clen, 4); pos += 4;
    if (cwd_len > 0) { memcpy(buf + pos, msg->new_tab.cwd, cwd_len); pos += cwd_len; }
    *out_buf = buf;
    *out_len = pos;
    return true;
}

bool wixen_ipc_msg_deserialize(const uint8_t *buf, size_t len, WixenIpcMessage *out_msg) {
    if (!buf || len < 16) return false; /* Minimum: magic + type + window_id + prof_len */

    /* Enforce max message size */
    if (len > WIXEN_IPC_MAX_MESSAGE_SIZE) return false;

    size_t pos = 0;
    uint32_t magic;
    memcpy(&magic, buf + pos, 4); pos += 4;
    if (magic != IPC_MAGIC) return false;
    memset(out_msg, 0, sizeof(*out_msg));
    uint32_t type;
    memcpy(&type, buf + pos, 4); pos += 4;
    out_msg->type = (WixenIpcMessageType)type;
    memcpy(&out_msg->window_id, buf + pos, 4); pos += 4;
    /* Profile */
    uint32_t plen;
    memcpy(&plen, buf + pos, 4); pos += 4;
    if (plen > WIXEN_IPC_MAX_MESSAGE_SIZE || pos + plen > len) return false;
    if (plen > 0) {
        out_msg->new_tab.profile = malloc(plen + 1);
        if (out_msg->new_tab.profile) {
            memcpy(out_msg->new_tab.profile, buf + pos, plen);
            out_msg->new_tab.profile[plen] = '\0';
        }
        pos += plen;
    }
    /* CWD */
    if (pos + 4 <= len) {
        uint32_t clen;
        memcpy(&clen, buf + pos, 4); pos += 4;
        if (clen > WIXEN_IPC_MAX_MESSAGE_SIZE) {
            wixen_ipc_msg_free(out_msg);
            return false;
        }
        if (pos + clen <= len && clen > 0) {
            out_msg->new_tab.cwd = malloc(clen + 1);
            if (out_msg->new_tab.cwd) {
                memcpy(out_msg->new_tab.cwd, buf + pos, clen);
                out_msg->new_tab.cwd[clen] = '\0';
            }
        }
    }
    return true;
}

void wixen_ipc_msg_free(WixenIpcMessage *msg) {
    free(msg->new_tab.profile);
    free(msg->new_tab.cwd);
    msg->new_tab.profile = NULL;
    msg->new_tab.cwd = NULL;
}

/* --- Internal helpers: serialize to pipe / deserialize from pipe --- */

static bool ipc_pipe_write_serialized(HANDLE pipe, const WixenIpcMessage *msg) {
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!wixen_ipc_msg_serialize(msg, &buf, &len)) return false;
    DWORD bytes_written;
    bool ok = WriteFile(pipe, buf, (DWORD)len, &bytes_written, NULL) != 0;
    free(buf);
    return ok;
}

static bool ipc_pipe_read_deserialized(HANDLE pipe, WixenIpcMessage *msg) {
    uint8_t raw[WIXEN_IPC_MAX_MESSAGE_SIZE];
    DWORD bytes_read = 0;
    if (!ReadFile(pipe, raw, sizeof(raw), &bytes_read, NULL)) return false;
    if (bytes_read < 16) return false;
    return wixen_ipc_msg_deserialize(raw, bytes_read, msg);
}

/* --- Server --- */

bool wixen_ipc_server_create(WixenIpcServer *server) {
    memset(server, 0, sizeof(*server));
    server->pipe = CreateNamedPipeW(
        WIXEN_PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,  /* Max instances */
        WIXEN_IPC_BUFFER_SIZE,
        WIXEN_IPC_BUFFER_SIZE,
        0,  /* Default timeout */
        NULL);
    return server->pipe != INVALID_HANDLE_VALUE;
}

bool wixen_ipc_server_accept(WixenIpcServer *server) {
    if (!server->pipe || server->pipe == INVALID_HANDLE_VALUE) return false;
    server->listening = ConnectNamedPipe(server->pipe, NULL) != 0
                        || GetLastError() == ERROR_PIPE_CONNECTED;
    return server->listening;
}

bool wixen_ipc_server_read(WixenIpcServer *server, WixenIpcMessage *msg) {
    return ipc_pipe_read_deserialized(server->pipe, msg);
}

bool wixen_ipc_server_write(WixenIpcServer *server, const WixenIpcMessage *msg) {
    return ipc_pipe_write_serialized(server->pipe, msg);
}

void wixen_ipc_server_disconnect(WixenIpcServer *server) {
    if (server->listening) {
        DisconnectNamedPipe(server->pipe);
        server->listening = false;
    }
}

void wixen_ipc_server_destroy(WixenIpcServer *server) {
    wixen_ipc_server_disconnect(server);
    if (server->pipe && server->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(server->pipe);
        server->pipe = INVALID_HANDLE_VALUE;
    }
}

/* --- Client --- */

bool wixen_ipc_client_connect(WixenIpcClient *client) {
    memset(client, 0, sizeof(*client));
    client->pipe = CreateFileW(
        WIXEN_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (client->pipe == INVALID_HANDLE_VALUE) return false;
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(client->pipe, &mode, NULL, NULL);
    return true;
}

bool wixen_ipc_client_send(WixenIpcClient *client, const WixenIpcMessage *msg) {
    return ipc_pipe_write_serialized(client->pipe, msg);
}

bool wixen_ipc_client_recv(WixenIpcClient *client, WixenIpcMessage *msg) {
    return ipc_pipe_read_deserialized(client->pipe, msg);
}

void wixen_ipc_client_disconnect(WixenIpcClient *client) {
    if (client->pipe && client->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(client->pipe);
        client->pipe = INVALID_HANDLE_VALUE;
    }
}

/* --- Check for existing server --- */

bool wixen_ipc_server_exists(void) {
    HANDLE pipe = CreateFileW(
        WIXEN_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
        return true;
    }
    return false;
}

void wixen_ipc_pipe_name(char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "\\\\.\\pipe\\wixen-terminal-ipc");
}

#endif /* _WIN32 */
