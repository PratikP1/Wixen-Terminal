/* serial.h — Serial port configuration */
#ifndef WIXEN_CONFIG_SERIAL_H
#define WIXEN_CONFIG_SERIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum { WIXEN_PARITY_NONE, WIXEN_PARITY_EVEN, WIXEN_PARITY_ODD } WixenParity;
typedef enum { WIXEN_FLOW_NONE, WIXEN_FLOW_HARDWARE, WIXEN_FLOW_SOFTWARE } WixenFlowControl;

typedef struct {
    char port[16];        /* "COM3" */
    uint32_t baud_rate;
    uint8_t data_bits;    /* 5-8 */
    uint8_t stop_bits;    /* 1-2 */
    WixenParity parity;
    WixenFlowControl flow_control;
} WixenSerialConfig;

void wixen_serial_config_default(WixenSerialConfig *cfg);
bool wixen_serial_validate(const WixenSerialConfig *cfg, char *err_buf, size_t err_buf_size);

#endif /* WIXEN_CONFIG_SERIAL_H */
