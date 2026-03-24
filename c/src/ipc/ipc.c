/* ipc.c — Named pipe IPC for multi-window */
#ifdef _WIN32

#include "wixen/ipc/ipc.h"
#include <string.h>

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
    DWORD bytes_read;
    if (!ReadFile(server->pipe, msg, sizeof(*msg), &bytes_read, NULL)) return false;
    return bytes_read >= sizeof(WixenIpcMessageType);
}

bool wixen_ipc_server_write(WixenIpcServer *server, const WixenIpcMessage *msg) {
    DWORD bytes_written;
    size_t total = sizeof(WixenIpcMessageType) + msg->payload_len;
    return WriteFile(server->pipe, msg, (DWORD)total, &bytes_written, NULL) != 0;
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
    DWORD bytes_written;
    size_t total = sizeof(WixenIpcMessageType) + msg->payload_len;
    return WriteFile(client->pipe, msg, (DWORD)total, &bytes_written, NULL) != 0;
}

bool wixen_ipc_client_recv(WixenIpcClient *client, WixenIpcMessage *msg) {
    DWORD bytes_read;
    if (!ReadFile(client->pipe, msg, sizeof(*msg), &bytes_read, NULL)) return false;
    return bytes_read >= sizeof(WixenIpcMessageType);
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

#endif /* _WIN32 */
