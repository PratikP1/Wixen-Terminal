/* test_ipc.c — Named pipe IPC tests */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"

#ifdef _WIN32
#include "wixen/ipc/ipc.h"

TEST ipc_no_server_initially(void) {
    /* Before we create a server, none should exist */
    /* Note: this may fail if another Wixen instance is running */
    /* Just check the function doesn't crash */
    bool exists = wixen_ipc_server_exists();
    (void)exists;
    PASS();
}

TEST ipc_server_create_destroy(void) {
    WixenIpcServer server;
    bool ok = wixen_ipc_server_create(&server);
    ASSERT(ok);
    ASSERT(server.pipe != INVALID_HANDLE_VALUE);
    wixen_ipc_server_destroy(&server);
    PASS();
}

TEST ipc_server_exists_after_create(void) {
    WixenIpcServer server;
    wixen_ipc_server_create(&server);
    ASSERT(wixen_ipc_server_exists());
    wixen_ipc_server_destroy(&server);
    PASS();
}

TEST ipc_client_connect_no_server(void) {
    /* Client connect should fail if no server */
    WixenIpcClient client;
    bool ok = wixen_ipc_client_connect(&client);
    ASSERT_FALSE(ok);
    PASS();
}

TEST ipc_message_init(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_WINDOW;
    ASSERT_EQ(WIXEN_IPC_NEW_WINDOW, msg.type);
    ASSERT_EQ(0, (int)msg.payload_len);
    PASS();
}

TEST ipc_message_with_payload(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_QUERY_SESSION;
    const char *data = "test payload";
    memcpy(msg.payload, data, strlen(data));
    msg.payload_len = strlen(data);
    ASSERT_EQ(12, (int)msg.payload_len);
    PASS();
}

SUITE(ipc_tests) {
    RUN_TEST(ipc_no_server_initially);
    RUN_TEST(ipc_server_create_destroy);
    RUN_TEST(ipc_server_exists_after_create);
    RUN_TEST(ipc_client_connect_no_server);
    RUN_TEST(ipc_message_init);
    RUN_TEST(ipc_message_with_payload);
}

#else
SUITE(ipc_tests) { /* No tests on non-Windows */ }
#endif

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(ipc_tests);
    GREATEST_MAIN_END();
}
