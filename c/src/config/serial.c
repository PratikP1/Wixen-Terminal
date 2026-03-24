/* serial.c — Serial port configuration */
#include "wixen/config/serial.h"
#include <string.h>
#include <stdio.h>

void wixen_serial_config_default(WixenSerialConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->port, "COM1", sizeof(cfg->port) - 1);
    cfg->baud_rate = 9600;
    cfg->data_bits = 8;
    cfg->stop_bits = 1;
    cfg->parity = WIXEN_PARITY_NONE;
    cfg->flow_control = WIXEN_FLOW_NONE;
}

bool wixen_serial_validate(const WixenSerialConfig *cfg, char *err_buf, size_t err_buf_size) {
    if (cfg->port[0] == '\0') {
        snprintf(err_buf, err_buf_size, "Port name is empty");
        return false;
    }
    if (cfg->baud_rate == 0) {
        snprintf(err_buf, err_buf_size, "Baud rate must be > 0");
        return false;
    }
    if (cfg->data_bits < 5 || cfg->data_bits > 8) {
        snprintf(err_buf, err_buf_size, "Data bits must be 5-8, got %d", cfg->data_bits);
        return false;
    }
    if (cfg->stop_bits < 1 || cfg->stop_bits > 2) {
        snprintf(err_buf, err_buf_size, "Stop bits must be 1-2, got %d", cfg->stop_bits);
        return false;
    }
    return true;
}
