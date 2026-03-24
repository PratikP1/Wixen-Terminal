/* ipc.h — Named pipe IPC for multi-window coordination */
#ifndef WIXEN_IPC_H
#define WIXEN_IPC_H

#ifdef _WIN32

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

#define WIXEN_PIPE_NAME L"\\\\.\\pipe\\wixen-terminal-ipc"
#define WIXEN_IPC_BUFFER_SIZE 4096

typedef enum {
    WIXEN_IPC_NEW_WINDOW = 1,
    WIXEN_IPC_JOIN_WINDOW,
    WIXEN_IPC_QUERY_SESSION,
    WIXEN_IPC_RESPONSE_OK,
    WIXEN_IPC_RESPONSE_ERROR,
} WixenIpcMessageType;

typedef struct {
    WixenIpcMessageType type;
    char payload[WIXEN_IPC_BUFFER_SIZE];
    size_t payload_len;
} WixenIpcMessage;

/* Server (daemon) */
typedef struct {
    HANDLE pipe;
    bool listening;
} WixenIpcServer;

bool wixen_ipc_server_create(WixenIpcServer *server);
bool wixen_ipc_server_accept(WixenIpcServer *server);
bool wixen_ipc_server_read(WixenIpcServer *server, WixenIpcMessage *msg);
bool wixen_ipc_server_write(WixenIpcServer *server, const WixenIpcMessage *msg);
void wixen_ipc_server_disconnect(WixenIpcServer *server);
void wixen_ipc_server_destroy(WixenIpcServer *server);

/* Client */
typedef struct {
    HANDLE pipe;
} WixenIpcClient;

bool wixen_ipc_client_connect(WixenIpcClient *client);
bool wixen_ipc_client_send(WixenIpcClient *client, const WixenIpcMessage *msg);
bool wixen_ipc_client_recv(WixenIpcClient *client, WixenIpcMessage *msg);
void wixen_ipc_client_disconnect(WixenIpcClient *client);

/* Check if a server is already running */
bool wixen_ipc_server_exists(void);

#endif /* _WIN32 */
#endif /* WIXEN_IPC_H */
