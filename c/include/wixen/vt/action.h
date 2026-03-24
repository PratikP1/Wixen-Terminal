/* action.h — VT parser actions (output from state machine) */
#ifndef WIXEN_VT_ACTION_H
#define WIXEN_VT_ACTION_H

#include <stdint.h>
#include <stddef.h>

#define WIXEN_MAX_PARAMS       32
#define WIXEN_MAX_SUBPARAMS    8
#define WIXEN_MAX_INTERMEDIATES 4
#define WIXEN_MAX_OSC_SEGMENTS 16
#define WIXEN_MAX_OSC_LEN      4096
#define WIXEN_MAX_DCS_LEN      65536
#define WIXEN_MAX_APC_LEN      4194304  /* 4 MB for Kitty graphics */

typedef enum {
    WIXEN_ACTION_NONE = 0,
    WIXEN_ACTION_PRINT,         /* Print character */
    WIXEN_ACTION_EXECUTE,       /* C0/C1 control byte */
    WIXEN_ACTION_CSI_DISPATCH,  /* CSI sequence complete */
    WIXEN_ACTION_ESC_DISPATCH,  /* ESC sequence complete */
    WIXEN_ACTION_OSC_DISPATCH,  /* OSC string complete */
    WIXEN_ACTION_DCS_HOOK,      /* DCS sequence start */
    WIXEN_ACTION_DCS_PUT,       /* DCS data byte */
    WIXEN_ACTION_DCS_UNHOOK,    /* DCS sequence end */
    WIXEN_ACTION_APC_DISPATCH,  /* APC string complete */
} WixenActionType;

typedef struct {
    WixenActionType type;
    union {
        /* PRINT */
        uint32_t codepoint;

        /* EXECUTE */
        uint8_t control_byte;

        /* CSI_DISPATCH */
        struct {
            uint16_t params[WIXEN_MAX_PARAMS];
            uint8_t param_count;
            uint16_t subparams[WIXEN_MAX_PARAMS][WIXEN_MAX_SUBPARAMS];
            uint8_t subparam_counts[WIXEN_MAX_PARAMS];
            uint8_t intermediates[WIXEN_MAX_INTERMEDIATES];
            uint8_t intermediate_count;
            char final_byte;
        } csi;

        /* ESC_DISPATCH */
        struct {
            uint8_t intermediates[WIXEN_MAX_INTERMEDIATES];
            uint8_t intermediate_count;
            char final_byte;
        } esc;

        /* OSC_DISPATCH */
        struct {
            uint8_t *data;      /* Raw OSC payload (caller owns) */
            size_t data_len;
        } osc;

        /* DCS_HOOK */
        struct {
            uint16_t params[WIXEN_MAX_PARAMS];
            uint8_t param_count;
            uint8_t intermediates[WIXEN_MAX_INTERMEDIATES];
            uint8_t intermediate_count;
            char final_byte;
        } dcs_hook;

        /* DCS_PUT */
        uint8_t dcs_byte;

        /* APC_DISPATCH */
        struct {
            uint8_t *data;      /* Raw APC payload (caller owns) */
            size_t data_len;
        } apc;
    };
} WixenAction;

#endif /* WIXEN_VT_ACTION_H */
