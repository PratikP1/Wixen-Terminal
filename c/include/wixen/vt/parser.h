/* parser.h — DEC VT500 state machine parser */
#ifndef WIXEN_VT_PARSER_H
#define WIXEN_VT_PARSER_H

#include "wixen/vt/action.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    WIXEN_PS_GROUND = 0,
    WIXEN_PS_ESCAPE,
    WIXEN_PS_ESCAPE_INTERMEDIATE,
    WIXEN_PS_CSI_ENTRY,
    WIXEN_PS_CSI_PARAM,
    WIXEN_PS_CSI_INTERMEDIATE,
    WIXEN_PS_CSI_IGNORE,
    WIXEN_PS_OSC_STRING,
    WIXEN_PS_DCS_ENTRY,
    WIXEN_PS_DCS_PARAM,
    WIXEN_PS_DCS_INTERMEDIATE,
    WIXEN_PS_DCS_PASSTHROUGH,
    WIXEN_PS_DCS_IGNORE,
    WIXEN_PS_SOS_PM_APC_STRING,
    WIXEN_PS_APC_STRING,
} WixenParserState;

typedef struct {
    WixenParserState state;

    /* CSI/DCS parameter accumulation */
    uint16_t params[WIXEN_MAX_PARAMS];
    uint8_t param_count;
    uint16_t current_param;
    bool has_param;

    /* Colon-delimited subparameters */
    uint16_t subparams[WIXEN_MAX_PARAMS][WIXEN_MAX_SUBPARAMS];
    uint8_t subparam_counts[WIXEN_MAX_PARAMS];
    uint16_t current_subparams[WIXEN_MAX_SUBPARAMS];
    uint8_t current_subparam_count;
    uint16_t current_subparam_value;
    bool has_subparam_digit;
    bool collecting_subparam;

    /* Intermediates (0x20-0x2F) */
    uint8_t intermediates[WIXEN_MAX_INTERMEDIATES];
    uint8_t intermediate_count;

    /* OSC accumulator */
    uint8_t *osc_data;
    size_t osc_len;
    size_t osc_cap;

    /* APC accumulator */
    uint8_t *apc_data;
    size_t apc_len;
    size_t apc_cap;

    /* UTF-8 decoder */
    uint8_t utf8_buf[4];
    uint8_t utf8_len;
    uint8_t utf8_idx;
} WixenParser;

void wixen_parser_init(WixenParser *p);
void wixen_parser_free(WixenParser *p);
void wixen_parser_reset(WixenParser *p);

/* Feed a single byte. Emits 0 or 1 actions. */
void wixen_parser_advance(WixenParser *p, uint8_t byte, WixenAction *out);

/* Feed multiple bytes. out_actions must have room for out_cap actions.
   Returns number of actions emitted. */
size_t wixen_parser_process(WixenParser *p, const uint8_t *data, size_t len,
                            WixenAction *out_actions, size_t out_cap);

#endif /* WIXEN_VT_PARSER_H */
