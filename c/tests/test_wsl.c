/* test_wsl.c — Tests for WSL config */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/config/wsl.h"

TEST wsl_list_init(void) {
    WixenWslList list;
    wixen_wsl_list_init(&list);
    ASSERT_EQ(0, (int)list.count);
    wixen_wsl_list_free(&list);
    PASS();
}

TEST wsl_list_add(void) {
    WixenWslList list;
    wixen_wsl_list_init(&list);
    wixen_wsl_list_add(&list, "Ubuntu", true);
    wixen_wsl_list_add(&list, "Debian", false);
    ASSERT_EQ(2, (int)list.count);
    ASSERT_STR_EQ("Ubuntu", list.distros[0].name);
    ASSERT(list.distros[0].is_default);
    ASSERT_STR_EQ("Debian", list.distros[1].name);
    ASSERT_FALSE(list.distros[1].is_default);
    wixen_wsl_list_free(&list);
    PASS();
}

TEST wsl_to_command_named(void) {
    char buf[256];
    ASSERT(wixen_wsl_to_command("Ubuntu", buf, sizeof(buf)));
    ASSERT_STR_EQ("wsl.exe -d Ubuntu", buf);
    PASS();
}

TEST wsl_to_command_default(void) {
    char buf[256];
    ASSERT(wixen_wsl_to_command(NULL, buf, sizeof(buf)));
    ASSERT_STR_EQ("wsl.exe", buf);
    PASS();
}

TEST wsl_to_command_empty(void) {
    char buf[256];
    ASSERT(wixen_wsl_to_command("", buf, sizeof(buf)));
    ASSERT_STR_EQ("wsl.exe", buf);
    PASS();
}

SUITE(wsl_tests) {
    RUN_TEST(wsl_list_init);
    RUN_TEST(wsl_list_add);
    RUN_TEST(wsl_to_command_named);
    RUN_TEST(wsl_to_command_default);
    RUN_TEST(wsl_to_command_empty);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(wsl_tests);
    GREATEST_MAIN_END();
}
