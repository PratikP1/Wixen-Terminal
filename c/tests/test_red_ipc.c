/* test_red_ipc.c — RED tests for multi-window IPC via named pipes
 *
 * The IPC system allows multiple Wixen windows to communicate:
 * - New window requests join existing server
 * - Server broadcasts config changes
 * - Tab transfer between windows
 */
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "greatest.h"
#include "wixen/ipc/ipc.h"

/* === Message serialization === */

TEST red_ipc_msg_serialize_new_tab(void) {
    WixenIpcMessage msg = {0};
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.new_tab.profile = "PowerShell";
    msg.new_tab.cwd = "C:\\Projects";
    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));
    ASSERT(buf != NULL);
    ASSERT(len > 0);

    WixenIpcMessage decoded = {0};
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &decoded));
    ASSERT_EQ(WIXEN_IPC_NEW_TAB, decoded.type);
    ASSERT_STR_EQ("PowerShell", decoded.new_tab.profile);
    ASSERT_STR_EQ("C:\\Projects", decoded.new_tab.cwd);

    wixen_ipc_msg_free(&decoded);
    free(buf);
    PASS();
}

TEST red_ipc_msg_serialize_focus(void) {
    WixenIpcMessage msg = {0};
    msg.type = WIXEN_IPC_FOCUS_WINDOW;
    msg.window_id = 42;
    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    WixenIpcMessage decoded = {0};
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &decoded));
    ASSERT_EQ(WIXEN_IPC_FOCUS_WINDOW, decoded.type);
    ASSERT_EQ(42, (int)decoded.window_id);

    wixen_ipc_msg_free(&decoded);
    free(buf);
    PASS();
}

TEST red_ipc_msg_empty_roundtrip(void) {
    WixenIpcMessage msg = {0};
    msg.type = WIXEN_IPC_PING;
    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    WixenIpcMessage decoded = {0};
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &decoded));
    ASSERT_EQ(WIXEN_IPC_PING, decoded.type);

    wixen_ipc_msg_free(&decoded);
    free(buf);
    PASS();
}

TEST red_ipc_msg_deserialize_garbage(void) {
    uint8_t garbage[] = {0xFF, 0xFE, 0x00, 0x01};
    WixenIpcMessage decoded = {0};
    ASSERT_FALSE(wixen_ipc_msg_deserialize(garbage, sizeof(garbage), &decoded));
    PASS();
}

/* === Pipe name generation === */

TEST red_ipc_pipe_name(void) {
    char name[128];
    wixen_ipc_pipe_name(name, sizeof(name));
    /* Should start with \\.\pipe\ */
    ASSERT(strstr(name, "\\\\.\\pipe\\") != NULL || strstr(name, "wixen") != NULL);
    PASS();
}

SUITE(red_ipc) {
    RUN_TEST(red_ipc_msg_serialize_new_tab);
    RUN_TEST(red_ipc_msg_serialize_focus);
    RUN_TEST(red_ipc_msg_empty_roundtrip);
    RUN_TEST(red_ipc_msg_deserialize_garbage);
    RUN_TEST(red_ipc_pipe_name);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_ipc);
    GREATEST_MAIN_END();
}
