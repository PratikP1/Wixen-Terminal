/* test_red_ipc_wire.c — RED tests for IPC wire format correctness (P0.2)
 *
 * Verifies that the IPC subsystem uses a proper serialized wire format
 * instead of raw struct transport over named pipes.
 */
#ifdef _WIN32

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "greatest.h"
#include "wixen/ipc/ipc.h"

/* ---- Round-trip serialize/deserialize ---- */

TEST red_wire_roundtrip_normal(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 42;
    msg.new_tab.profile = _strdup("PowerShell");
    msg.new_tab.cwd = _strdup("C:\\Users\\test");

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));
    ASSERT(buf != NULL);
    ASSERT(len > 0);

    WixenIpcMessage out;
    memset(&out, 0, sizeof(out));
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &out));

    ASSERT_EQ(msg.type, out.type);
    ASSERT_EQ(msg.window_id, out.window_id);
    ASSERT(out.new_tab.profile != NULL);
    ASSERT_STR_EQ("PowerShell", out.new_tab.profile);
    ASSERT(out.new_tab.cwd != NULL);
    ASSERT_STR_EQ("C:\\Users\\test", out.new_tab.cwd);

    wixen_ipc_msg_free(&out);
    free(msg.new_tab.profile);
    free(msg.new_tab.cwd);
    free(buf);
    PASS();
}

TEST red_wire_roundtrip_all_types(void) {
    WixenIpcMessageType types[] = {
        WIXEN_IPC_NEW_WINDOW, WIXEN_IPC_JOIN_WINDOW, WIXEN_IPC_QUERY_SESSION,
        WIXEN_IPC_RESPONSE_OK, WIXEN_IPC_RESPONSE_ERROR, WIXEN_IPC_NEW_TAB,
        WIXEN_IPC_FOCUS_WINDOW, WIXEN_IPC_PING, WIXEN_IPC_CONFIG_CHANGED,
    };
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        WixenIpcMessage msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = types[i];
        msg.window_id = (uint32_t)i;

        uint8_t *buf = NULL;
        size_t len = 0;
        ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

        WixenIpcMessage out;
        ASSERT(wixen_ipc_msg_deserialize(buf, len, &out));
        ASSERT_EQ(types[i], out.type);
        ASSERT_EQ((uint32_t)i, out.window_id);

        wixen_ipc_msg_free(&out);
        free(buf);
    }
    PASS();
}

/* ---- Empty optional strings ---- */

TEST red_wire_empty_strings(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 7;
    msg.new_tab.profile = NULL;
    msg.new_tab.cwd = NULL;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    WixenIpcMessage out;
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &out));
    ASSERT_EQ(WIXEN_IPC_NEW_TAB, out.type);
    ASSERT_EQ(7u, out.window_id);
    ASSERT(out.new_tab.profile == NULL);
    ASSERT(out.new_tab.cwd == NULL);

    wixen_ipc_msg_free(&out);
    free(buf);
    PASS();
}

TEST red_wire_profile_only(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 1;
    msg.new_tab.profile = _strdup("cmd");
    msg.new_tab.cwd = NULL;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    WixenIpcMessage out;
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &out));
    ASSERT_STR_EQ("cmd", out.new_tab.profile);
    ASSERT(out.new_tab.cwd == NULL);

    wixen_ipc_msg_free(&out);
    free(msg.new_tab.profile);
    free(buf);
    PASS();
}

TEST red_wire_cwd_only(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 1;
    msg.new_tab.profile = NULL;
    msg.new_tab.cwd = _strdup("D:\\work");

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    WixenIpcMessage out;
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &out));
    ASSERT(out.new_tab.profile == NULL);
    ASSERT(out.new_tab.cwd != NULL);
    ASSERT_STR_EQ("D:\\work", out.new_tab.cwd);

    wixen_ipc_msg_free(&out);
    free(msg.new_tab.cwd);
    free(buf);
    PASS();
}

/* ---- Truncated header rejection ---- */

TEST red_wire_reject_truncated_zero(void) {
    WixenIpcMessage out;
    ASSERT_FALSE(wixen_ipc_msg_deserialize(NULL, 0, &out));
    PASS();
}

TEST red_wire_reject_truncated_short(void) {
    uint8_t buf[8] = {0};
    WixenIpcMessage out;
    /* Less than minimum header (magic+type+window_id+prof_len = 16 bytes) */
    ASSERT_FALSE(wixen_ipc_msg_deserialize(buf, 8, &out));
    PASS();
}

TEST red_wire_reject_truncated_15(void) {
    uint8_t buf[15] = {0};
    WixenIpcMessage out;
    /* Exactly one byte short of minimum */
    ASSERT_FALSE(wixen_ipc_msg_deserialize(buf, 15, &out));
    PASS();
}

TEST red_wire_reject_truncated_profile(void) {
    /* Build a valid header but claim profile is longer than remaining data */
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_PING;
    msg.window_id = 1;
    msg.new_tab.profile = _strdup("hello");

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    /* Truncate: give it only up through the profile length field, not the data */
    WixenIpcMessage out;
    ASSERT_FALSE(wixen_ipc_msg_deserialize(buf, 16, &out));

    free(msg.new_tab.profile);
    free(buf);
    PASS();
}

/* ---- Bad magic rejection ---- */

TEST red_wire_reject_bad_magic(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_PING;
    msg.window_id = 1;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));

    /* Corrupt the magic bytes */
    buf[0] = 0xDE;
    buf[1] = 0xAD;
    buf[2] = 0xBE;
    buf[3] = 0xEF;

    WixenIpcMessage out;
    ASSERT_FALSE(wixen_ipc_msg_deserialize(buf, len, &out));

    free(buf);
    PASS();
}

TEST red_wire_reject_zero_magic(void) {
    uint8_t buf[20];
    memset(buf, 0, sizeof(buf));
    WixenIpcMessage out;
    ASSERT_FALSE(wixen_ipc_msg_deserialize(buf, sizeof(buf), &out));
    PASS();
}

/* ---- Magic header present in serialized output ---- */

TEST red_wire_magic_header_present(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_PING;
    msg.window_id = 0;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));
    ASSERT(len >= 4);

    /* First 4 bytes must be IPC_MAGIC = 0x57495843 ("WIXC") */
    uint32_t magic;
    memcpy(&magic, buf, 4);
    ASSERT_EQ(0x57495843u, magic);

    free(buf);
    PASS();
}

/* ---- Oversized payload rejection ---- */

TEST red_wire_reject_oversized_profile(void) {
    /* Serialize should reject when total message exceeds max */
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 1;

    /* Create a profile string larger than WIXEN_IPC_MAX_MESSAGE_SIZE */
    size_t huge = WIXEN_IPC_MAX_MESSAGE_SIZE + 1;
    char *big = malloc(huge);
    ASSERT(big != NULL);
    memset(big, 'A', huge - 1);
    big[huge - 1] = '\0';
    msg.new_tab.profile = big;
    msg.new_tab.cwd = NULL;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT_FALSE(wixen_ipc_msg_serialize(&msg, &buf, &len));

    free(big);
    PASS();
}

TEST red_wire_reject_oversized_cwd(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 1;
    msg.new_tab.profile = NULL;

    size_t huge = WIXEN_IPC_MAX_MESSAGE_SIZE + 1;
    char *big = malloc(huge);
    ASSERT(big != NULL);
    memset(big, 'B', huge - 1);
    big[huge - 1] = '\0';
    msg.new_tab.cwd = big;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT_FALSE(wixen_ipc_msg_serialize(&msg, &buf, &len));

    free(big);
    PASS();
}

TEST red_wire_reject_oversized_deserialized(void) {
    /* Craft a buffer that claims a string length exceeding max */
    uint8_t buf[24];
    size_t pos = 0;
    uint32_t magic = 0x57495843;
    memcpy(buf + pos, &magic, 4); pos += 4;
    uint32_t type = WIXEN_IPC_NEW_TAB;
    memcpy(buf + pos, &type, 4); pos += 4;
    uint32_t wid = 1;
    memcpy(buf + pos, &wid, 4); pos += 4;
    /* Claim profile length is enormous */
    uint32_t plen = WIXEN_IPC_MAX_MESSAGE_SIZE + 1;
    memcpy(buf + pos, &plen, 4); pos += 4;

    WixenIpcMessage out;
    ASSERT_FALSE(wixen_ipc_msg_deserialize(buf, sizeof(buf), &out));
    PASS();
}

/* ---- MAX message size enforcement ---- */

TEST red_wire_max_size_constant(void) {
    /* WIXEN_IPC_MAX_MESSAGE_SIZE must be defined and reasonable */
    ASSERT(WIXEN_IPC_MAX_MESSAGE_SIZE > 0);
    ASSERT(WIXEN_IPC_MAX_MESSAGE_SIZE <= 1048576); /* At most 1MB */
    PASS();
}

TEST red_wire_exactly_at_max(void) {
    /* A message that is exactly at the max size limit should succeed */
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 1;

    /* Header overhead: magic(4) + type(4) + wid(4) + proflen(4) + cwdlen(4) = 20 */
    size_t overhead = 20;
    size_t max_str = WIXEN_IPC_MAX_MESSAGE_SIZE - overhead;
    char *str = malloc(max_str + 1);
    ASSERT(str != NULL);
    memset(str, 'X', max_str);
    str[max_str] = '\0';
    msg.new_tab.profile = str;
    msg.new_tab.cwd = NULL;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT(wixen_ipc_msg_serialize(&msg, &buf, &len));
    ASSERT(len <= WIXEN_IPC_MAX_MESSAGE_SIZE);

    WixenIpcMessage out;
    ASSERT(wixen_ipc_msg_deserialize(buf, len, &out));
    ASSERT_EQ(max_str, strlen(out.new_tab.profile));

    wixen_ipc_msg_free(&out);
    free(str);
    free(buf);
    PASS();
}

TEST red_wire_one_over_max(void) {
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 1;

    size_t overhead = 20;
    size_t over_str = WIXEN_IPC_MAX_MESSAGE_SIZE - overhead + 1;
    char *str = malloc(over_str + 1);
    ASSERT(str != NULL);
    memset(str, 'Y', over_str);
    str[over_str] = '\0';
    msg.new_tab.profile = str;
    msg.new_tab.cwd = NULL;

    uint8_t *buf = NULL;
    size_t len = 0;
    ASSERT_FALSE(wixen_ipc_msg_serialize(&msg, &buf, &len));

    free(str);
    PASS();
}

/* ---- Pipe write/read must use serialized format ---- */

TEST red_wire_pipe_write_uses_serialized(void) {
    /* Create a server pipe and have client send a message.
     * Read raw bytes from the server side and verify they start with
     * the IPC magic, proving serialized format is used (not raw struct). */
    WixenIpcServer server;
    ASSERT(wixen_ipc_server_create(&server));

    /* Client connect in a simple blocking manner (same thread works for
     * message-mode pipes with single instance if we connect before accept) */
    WixenIpcClient client;
    /* We need to accept on another thread or connect first.
     * For testing: connect first, then accept (ERROR_PIPE_CONNECTED path). */
    ASSERT(wixen_ipc_client_connect(&client));
    ASSERT(wixen_ipc_server_accept(&server));

    /* Send a message via client */
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_PING;
    msg.window_id = 99;
    ASSERT(wixen_ipc_client_send(&client, &msg));

    /* Read raw bytes from server pipe to check wire format */
    uint8_t raw[WIXEN_IPC_BUFFER_SIZE];
    DWORD bytes_read = 0;
    ASSERT(ReadFile(server.pipe, raw, sizeof(raw), &bytes_read, NULL));

    /* The first 4 bytes must be the IPC magic, not a raw struct field */
    ASSERT(bytes_read >= 4);
    uint32_t wire_magic;
    memcpy(&wire_magic, raw, 4);
    ASSERT_EQ(0x57495843u, wire_magic);

    /* Also verify we can deserialize what was sent */
    WixenIpcMessage received;
    ASSERT(wixen_ipc_msg_deserialize(raw, bytes_read, &received));
    ASSERT_EQ(WIXEN_IPC_PING, received.type);
    ASSERT_EQ(99u, received.window_id);

    wixen_ipc_msg_free(&received);
    wixen_ipc_client_disconnect(&client);
    wixen_ipc_server_destroy(&server);
    PASS();
}

TEST red_wire_pipe_read_uses_deserialized(void) {
    /* Server writes a serialized message; client reads via recv which should
     * deserialize automatically and return a proper WixenIpcMessage. */
    WixenIpcServer server;
    ASSERT(wixen_ipc_server_create(&server));

    WixenIpcClient client;
    ASSERT(wixen_ipc_client_connect(&client));
    ASSERT(wixen_ipc_server_accept(&server));

    /* Server sends a message (should serialize internally) */
    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_NEW_TAB;
    msg.window_id = 55;
    msg.new_tab.profile = _strdup("bash");
    msg.new_tab.cwd = _strdup("C:\\home");
    ASSERT(wixen_ipc_server_write(&server, &msg));

    /* Client receives via recv (should deserialize internally) */
    WixenIpcMessage received;
    memset(&received, 0, sizeof(received));
    ASSERT(wixen_ipc_client_recv(&client, &received));
    ASSERT_EQ(WIXEN_IPC_NEW_TAB, received.type);
    ASSERT_EQ(55u, received.window_id);
    ASSERT(received.new_tab.profile != NULL);
    ASSERT_STR_EQ("bash", received.new_tab.profile);
    ASSERT(received.new_tab.cwd != NULL);
    ASSERT_STR_EQ("C:\\home", received.new_tab.cwd);

    wixen_ipc_msg_free(&received);
    free(msg.new_tab.profile);
    free(msg.new_tab.cwd);
    wixen_ipc_client_disconnect(&client);
    wixen_ipc_server_destroy(&server);
    PASS();
}

TEST red_wire_pipe_roundtrip_empty_strings(void) {
    WixenIpcServer server;
    ASSERT(wixen_ipc_server_create(&server));

    WixenIpcClient client;
    ASSERT(wixen_ipc_client_connect(&client));
    ASSERT(wixen_ipc_server_accept(&server));

    WixenIpcMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIXEN_IPC_FOCUS_WINDOW;
    msg.window_id = 3;
    msg.new_tab.profile = NULL;
    msg.new_tab.cwd = NULL;
    ASSERT(wixen_ipc_client_send(&client, &msg));

    WixenIpcMessage received;
    memset(&received, 0, sizeof(received));
    ASSERT(wixen_ipc_server_read(&server, &received));
    ASSERT_EQ(WIXEN_IPC_FOCUS_WINDOW, received.type);
    ASSERT_EQ(3u, received.window_id);
    ASSERT(received.new_tab.profile == NULL);
    ASSERT(received.new_tab.cwd == NULL);

    wixen_ipc_msg_free(&received);
    wixen_ipc_client_disconnect(&client);
    wixen_ipc_server_destroy(&server);
    PASS();
}

/* ---- Suites ---- */

SUITE(red_ipc_wire_serialization) {
    RUN_TEST(red_wire_roundtrip_normal);
    RUN_TEST(red_wire_roundtrip_all_types);
    RUN_TEST(red_wire_empty_strings);
    RUN_TEST(red_wire_profile_only);
    RUN_TEST(red_wire_cwd_only);
    RUN_TEST(red_wire_magic_header_present);
}

SUITE(red_ipc_wire_rejection) {
    RUN_TEST(red_wire_reject_truncated_zero);
    RUN_TEST(red_wire_reject_truncated_short);
    RUN_TEST(red_wire_reject_truncated_15);
    RUN_TEST(red_wire_reject_truncated_profile);
    RUN_TEST(red_wire_reject_bad_magic);
    RUN_TEST(red_wire_reject_zero_magic);
    RUN_TEST(red_wire_reject_oversized_profile);
    RUN_TEST(red_wire_reject_oversized_cwd);
    RUN_TEST(red_wire_reject_oversized_deserialized);
}

SUITE(red_ipc_wire_max_size) {
    RUN_TEST(red_wire_max_size_constant);
    RUN_TEST(red_wire_exactly_at_max);
    RUN_TEST(red_wire_one_over_max);
}

SUITE(red_ipc_wire_pipe) {
    RUN_TEST(red_wire_pipe_write_uses_serialized);
    RUN_TEST(red_wire_pipe_read_uses_deserialized);
    RUN_TEST(red_wire_pipe_roundtrip_empty_strings);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(red_ipc_wire_serialization);
    RUN_SUITE(red_ipc_wire_rejection);
    RUN_SUITE(red_ipc_wire_max_size);
    RUN_SUITE(red_ipc_wire_pipe);
    GREATEST_MAIN_END();
}

#else
/* Non-Windows: no-op */
int main(void) { return 0; }
#endif
