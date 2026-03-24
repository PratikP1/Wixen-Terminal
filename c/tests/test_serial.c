/* test_serial.c — Tests for serial port config */
#include <stdbool.h>
#include <string.h>
#include "greatest.h"
#include "wixen/config/serial.h"

TEST serial_defaults(void) {
    WixenSerialConfig cfg;
    wixen_serial_config_default(&cfg);
    ASSERT_STR_EQ("COM1", cfg.port);
    ASSERT_EQ(9600, (int)cfg.baud_rate);
    ASSERT_EQ(8, cfg.data_bits);
    ASSERT_EQ(1, cfg.stop_bits);
    ASSERT_EQ(WIXEN_PARITY_NONE, cfg.parity);
    ASSERT_EQ(WIXEN_FLOW_NONE, cfg.flow_control);
    PASS();
}

TEST serial_valid(void) {
    WixenSerialConfig cfg;
    wixen_serial_config_default(&cfg);
    char err[128];
    ASSERT(wixen_serial_validate(&cfg, err, sizeof(err)));
    PASS();
}

TEST serial_invalid_data_bits(void) {
    WixenSerialConfig cfg;
    wixen_serial_config_default(&cfg);
    cfg.data_bits = 4;
    char err[128];
    ASSERT_FALSE(wixen_serial_validate(&cfg, err, sizeof(err)));
    PASS();
}

TEST serial_invalid_baud(void) {
    WixenSerialConfig cfg;
    wixen_serial_config_default(&cfg);
    cfg.baud_rate = 0;
    char err[128];
    ASSERT_FALSE(wixen_serial_validate(&cfg, err, sizeof(err)));
    PASS();
}

TEST serial_invalid_stop_bits(void) {
    WixenSerialConfig cfg;
    wixen_serial_config_default(&cfg);
    cfg.stop_bits = 3;
    char err[128];
    ASSERT_FALSE(wixen_serial_validate(&cfg, err, sizeof(err)));
    PASS();
}

TEST serial_empty_port(void) {
    WixenSerialConfig cfg;
    wixen_serial_config_default(&cfg);
    cfg.port[0] = '\0';
    char err[128];
    ASSERT_FALSE(wixen_serial_validate(&cfg, err, sizeof(err)));
    PASS();
}

SUITE(serial_tests) {
    RUN_TEST(serial_defaults);
    RUN_TEST(serial_valid);
    RUN_TEST(serial_invalid_data_bits);
    RUN_TEST(serial_invalid_baud);
    RUN_TEST(serial_invalid_stop_bits);
    RUN_TEST(serial_empty_port);
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(serial_tests);
    GREATEST_MAIN_END();
}
