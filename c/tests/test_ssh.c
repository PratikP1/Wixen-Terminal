/* test_ssh.c — Tests for SSH URL parsing */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/config/ssh.h"

TEST ssh_parse_full(void) {
    WixenSshTarget t;
    ASSERT(wixen_ssh_parse_url("ssh://user@host.com:2222", &t));
    ASSERT_STR_EQ("host.com", t.host);
    ASSERT_STR_EQ("user", t.user);
    ASSERT_EQ(2222, t.port);
    wixen_ssh_target_free(&t);
    PASS();
}

TEST ssh_parse_no_port(void) {
    WixenSshTarget t;
    ASSERT(wixen_ssh_parse_url("ssh://admin@server.com", &t));
    ASSERT_STR_EQ("server.com", t.host);
    ASSERT_STR_EQ("admin", t.user);
    ASSERT_EQ(22, t.port);
    wixen_ssh_target_free(&t);
    PASS();
}

TEST ssh_parse_no_user(void) {
    WixenSshTarget t;
    ASSERT(wixen_ssh_parse_url("ssh://host.com:8022", &t));
    ASSERT_STR_EQ("host.com", t.host);
    ASSERT(t.user == NULL);
    ASSERT_EQ(8022, t.port);
    wixen_ssh_target_free(&t);
    PASS();
}

TEST ssh_parse_bare_host(void) {
    WixenSshTarget t;
    ASSERT(wixen_ssh_parse_url("myserver.com", &t));
    ASSERT_STR_EQ("myserver.com", t.host);
    ASSERT_EQ(22, t.port);
    wixen_ssh_target_free(&t);
    PASS();
}

TEST ssh_parse_empty_fails(void) {
    WixenSshTarget t;
    ASSERT_FALSE(wixen_ssh_parse_url("", &t));
    ASSERT_FALSE(wixen_ssh_parse_url(NULL, &t));
    PASS();
}

TEST ssh_to_command_full(void) {
    WixenSshTarget t = { .host = "host.com", .user = "admin", .port = 2222 };
    char buf[256];
    ASSERT(wixen_ssh_to_command(&t, buf, sizeof(buf)));
    ASSERT_STR_EQ("ssh -p 2222 admin@host.com", buf);
    PASS();
}

TEST ssh_to_command_default_port(void) {
    WixenSshTarget t = { .host = "host.com", .user = "user", .port = 22 };
    char buf[256];
    ASSERT(wixen_ssh_to_command(&t, buf, sizeof(buf)));
    ASSERT_STR_EQ("ssh user@host.com", buf);
    PASS();
}

TEST ssh_to_command_no_user(void) {
    WixenSshTarget t = { .host = "host.com", .user = NULL, .port = 22 };
    char buf[256];
    ASSERT(wixen_ssh_to_command(&t, buf, sizeof(buf)));
    ASSERT_STR_EQ("ssh host.com", buf);
    PASS();
}

SUITE(ssh_tests) {
    RUN_TEST(ssh_parse_full);
    RUN_TEST(ssh_parse_no_port);
    RUN_TEST(ssh_parse_no_user);
    RUN_TEST(ssh_parse_bare_host);
    RUN_TEST(ssh_parse_empty_fails);
    RUN_TEST(ssh_to_command_full);
    RUN_TEST(ssh_to_command_default_port);
    RUN_TEST(ssh_to_command_no_user);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(ssh_tests);
    GREATEST_MAIN_END();
}
