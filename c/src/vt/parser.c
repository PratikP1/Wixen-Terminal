/* parser.c — DEC VT500 state machine parser implementation */
#include "wixen/vt/parser.h"
#include <stdlib.h>
#include <string.h>

/* --- Helpers --- */

static void clear_params(WixenParser *p) {
    memset(p->params, 0, sizeof(p->params));
    p->param_count = 0;
    p->current_param = 0;
    p->has_param = false;
    memset(p->subparams, 0, sizeof(p->subparams));
    memset(p->subparam_counts, 0, sizeof(p->subparam_counts));
    memset(p->current_subparams, 0, sizeof(p->current_subparams));
    p->current_subparam_count = 0;
    p->current_subparam_value = 0;
    p->has_subparam_digit = false;
    p->collecting_subparam = false;
}

static void clear_intermediates(WixenParser *p) {
    memset(p->intermediates, 0, sizeof(p->intermediates));
    p->intermediate_count = 0;
}

static void push_param(WixenParser *p) {
    if (p->param_count < WIXEN_MAX_PARAMS) {
        p->params[p->param_count] = p->has_param ? p->current_param : 0;
        /* Flush pending subparam value */
        if (p->collecting_subparam && p->has_subparam_digit) {
            if (p->current_subparam_count < WIXEN_MAX_SUBPARAMS) {
                p->current_subparams[p->current_subparam_count++] = p->current_subparam_value;
            }
        }
        /* Store accumulated subparams for this param */
        if (p->current_subparam_count > 0) {
            memcpy(p->subparams[p->param_count], p->current_subparams,
                   p->current_subparam_count * sizeof(uint16_t));
            p->subparam_counts[p->param_count] = p->current_subparam_count;
        }
        p->param_count++;
    }
    p->current_param = 0;
    p->has_param = false;
    p->current_subparam_count = 0;
    p->current_subparam_value = 0;
    p->has_subparam_digit = false;
    p->collecting_subparam = false;
}

static void finalize_params(WixenParser *p) {
    /* Push the last accumulated param */
    if (p->has_param || p->param_count > 0 || p->collecting_subparam) {
        push_param(p);
    }
}

static void collect_intermediate(WixenParser *p, uint8_t byte) {
    if (p->intermediate_count < WIXEN_MAX_INTERMEDIATES) {
        p->intermediates[p->intermediate_count++] = byte;
    }
}

static void osc_put(WixenParser *p, uint8_t byte) {
    if (p->osc_len >= p->osc_cap) {
        size_t new_cap = p->osc_cap ? p->osc_cap * 2 : 256;
        if (new_cap > WIXEN_MAX_OSC_LEN) new_cap = WIXEN_MAX_OSC_LEN;
        if (p->osc_len >= new_cap) return; /* Overflow protection */
        uint8_t *new_data = realloc(p->osc_data, new_cap);
        if (!new_data) return;
        p->osc_data = new_data;
        p->osc_cap = new_cap;
    }
    p->osc_data[p->osc_len++] = byte;
}

static void apc_put(WixenParser *p, uint8_t byte) {
    if (p->apc_len >= p->apc_cap) {
        size_t new_cap = p->apc_cap ? p->apc_cap * 2 : 4096;
        if (new_cap > WIXEN_MAX_APC_LEN) new_cap = WIXEN_MAX_APC_LEN;
        if (p->apc_len >= new_cap) return;
        uint8_t *new_data = realloc(p->apc_data, new_cap);
        if (!new_data) return;
        p->apc_data = new_data;
        p->apc_cap = new_cap;
    }
    p->apc_data[p->apc_len++] = byte;
}

static void emit_print(WixenAction *out, uint32_t cp) {
    out->type = WIXEN_ACTION_PRINT;
    out->codepoint = cp;
}

static void emit_execute(WixenAction *out, uint8_t byte) {
    out->type = WIXEN_ACTION_EXECUTE;
    out->control_byte = byte;
}

static void emit_csi(WixenParser *p, WixenAction *out, char final) {
    finalize_params(p);
    out->type = WIXEN_ACTION_CSI_DISPATCH;
    memcpy(out->csi.params, p->params, sizeof(p->params));
    out->csi.param_count = p->param_count;
    memcpy(out->csi.subparams, p->subparams, sizeof(p->subparams));
    memcpy(out->csi.subparam_counts, p->subparam_counts, sizeof(p->subparam_counts));
    memcpy(out->csi.intermediates, p->intermediates, sizeof(p->intermediates));
    out->csi.intermediate_count = p->intermediate_count;
    out->csi.final_byte = final;
}

static void emit_esc(WixenParser *p, WixenAction *out, char final) {
    out->type = WIXEN_ACTION_ESC_DISPATCH;
    memcpy(out->esc.intermediates, p->intermediates, sizeof(p->intermediates));
    out->esc.intermediate_count = p->intermediate_count;
    out->esc.final_byte = final;
}

static void emit_osc(WixenParser *p, WixenAction *out) {
    out->type = WIXEN_ACTION_OSC_DISPATCH;
    /* Transfer ownership of osc_data to action */
    out->osc.data = p->osc_data;
    out->osc.data_len = p->osc_len;
    /* Detach from parser (caller now owns the data) */
    p->osc_data = NULL;
    p->osc_len = 0;
    p->osc_cap = 0;
}

static void emit_apc(WixenParser *p, WixenAction *out) {
    out->type = WIXEN_ACTION_APC_DISPATCH;
    out->apc.data = p->apc_data;
    out->apc.data_len = p->apc_len;
    p->apc_data = NULL;
    p->apc_len = 0;
    p->apc_cap = 0;
}

/* --- UTF-8 decoder --- */

static bool utf8_start(WixenParser *p, uint8_t byte) {
    if ((byte & 0xE0) == 0xC0) {
        p->utf8_buf[0] = byte;
        p->utf8_len = 2;
        p->utf8_idx = 1;
        return true;
    }
    if ((byte & 0xF0) == 0xE0) {
        p->utf8_buf[0] = byte;
        p->utf8_len = 3;
        p->utf8_idx = 1;
        return true;
    }
    if ((byte & 0xF8) == 0xF0) {
        p->utf8_buf[0] = byte;
        p->utf8_len = 4;
        p->utf8_idx = 1;
        return true;
    }
    return false;
}

static bool utf8_continue(WixenParser *p, uint8_t byte, uint32_t *out_cp) {
    if ((byte & 0xC0) != 0x80) {
        /* Invalid continuation — emit replacement character */
        *out_cp = 0xFFFD;
        p->utf8_len = 0;
        p->utf8_idx = 0;
        return true;
    }
    p->utf8_buf[p->utf8_idx++] = byte;
    if (p->utf8_idx < p->utf8_len) return false; /* Need more bytes */

    /* Decode */
    uint32_t cp = 0;
    switch (p->utf8_len) {
    case 2:
        cp = ((uint32_t)(p->utf8_buf[0] & 0x1F) << 6)
           | (p->utf8_buf[1] & 0x3F);
        break;
    case 3:
        cp = ((uint32_t)(p->utf8_buf[0] & 0x0F) << 12)
           | ((uint32_t)(p->utf8_buf[1] & 0x3F) << 6)
           | (p->utf8_buf[2] & 0x3F);
        break;
    case 4:
        cp = ((uint32_t)(p->utf8_buf[0] & 0x07) << 18)
           | ((uint32_t)(p->utf8_buf[1] & 0x3F) << 12)
           | ((uint32_t)(p->utf8_buf[2] & 0x3F) << 6)
           | (p->utf8_buf[3] & 0x3F);
        break;
    }

    /* Validate: reject overlong, surrogates, beyond Unicode */
    if ((p->utf8_len == 2 && cp < 0x80)
        || (p->utf8_len == 3 && cp < 0x800)
        || (p->utf8_len == 4 && cp < 0x10000)
        || (cp >= 0xD800 && cp <= 0xDFFF)
        || cp > 0x10FFFF) {
        cp = 0xFFFD;
    }

    *out_cp = cp;
    p->utf8_len = 0;
    p->utf8_idx = 0;
    return true;
}

/* --- Lifecycle --- */

void wixen_parser_init(WixenParser *p) {
    memset(p, 0, sizeof(*p));
    p->state = WIXEN_PS_GROUND;
}

void wixen_parser_free(WixenParser *p) {
    free(p->osc_data);
    free(p->apc_data);
    p->osc_data = NULL;
    p->apc_data = NULL;
}

void wixen_parser_reset(WixenParser *p) {
    WixenParserState saved_state = p->state; (void)saved_state;
    free(p->osc_data);
    free(p->apc_data);
    memset(p, 0, sizeof(*p));
    p->state = WIXEN_PS_GROUND;
}

/* --- Main advance --- */

void wixen_parser_advance(WixenParser *p, uint8_t byte, WixenAction *out) {
    out->type = WIXEN_ACTION_NONE;

    /* Handle UTF-8 continuation in ground state */
    if (p->utf8_len > 0) {
        uint32_t cp;
        if (utf8_continue(p, byte, &cp)) {
            emit_print(out, cp);
        }
        return;
    }

    /* Anywhere transitions: ESC, CAN, SUB, C1 */
    if (byte == 0x1B && p->state != WIXEN_PS_OSC_STRING
        && p->state != WIXEN_PS_DCS_PASSTHROUGH
        && p->state != WIXEN_PS_SOS_PM_APC_STRING
        && p->state != WIXEN_PS_APC_STRING) {
        clear_params(p);
        clear_intermediates(p);
        p->state = WIXEN_PS_ESCAPE;
        return;
    }

    if (byte == 0x18 || byte == 0x1A) {
        /* CAN or SUB — cancel current sequence */
        p->state = WIXEN_PS_GROUND;
        return;
    }

    switch (p->state) {
    case WIXEN_PS_GROUND:
        if (byte < 0x20) {
            /* C0 controls */
            emit_execute(out, byte);
        } else if (byte < 0x80) {
            /* Printable ASCII */
            emit_print(out, byte);
        } else if (byte >= 0x80 && byte < 0xA0) {
            /* C1 controls (8-bit) */
            if (byte == 0x9B) {
                /* CSI */
                clear_params(p);
                clear_intermediates(p);
                p->state = WIXEN_PS_CSI_ENTRY;
            } else if (byte == 0x9D) {
                /* OSC */
                p->osc_len = 0;
                p->state = WIXEN_PS_OSC_STRING;
            } else if (byte == 0x90) {
                /* DCS */
                clear_params(p);
                clear_intermediates(p);
                p->state = WIXEN_PS_DCS_ENTRY;
            } else {
                emit_execute(out, byte);
            }
        } else {
            /* UTF-8 multi-byte start */
            if (!utf8_start(p, byte)) {
                /* Invalid UTF-8 start byte */
                emit_print(out, 0xFFFD);
            }
        }
        break;

    case WIXEN_PS_ESCAPE:
        if (byte == 0x5B) {
            /* ESC [ → CSI */
            clear_params(p);
            clear_intermediates(p);
            p->state = WIXEN_PS_CSI_ENTRY;
        } else if (byte == 0x5D) {
            /* ESC ] → OSC */
            p->osc_len = 0;
            p->state = WIXEN_PS_OSC_STRING;
        } else if (byte == 0x50) {
            /* ESC P → DCS */
            clear_params(p);
            clear_intermediates(p);
            p->state = WIXEN_PS_DCS_ENTRY;
        } else if (byte == 0x5F) {
            /* ESC _ → APC */
            p->apc_len = 0;
            p->state = WIXEN_PS_APC_STRING;
        } else if (byte == 0x58 || byte == 0x5E) {
            /* ESC X (SOS) or ESC ^ (PM) */
            p->state = WIXEN_PS_SOS_PM_APC_STRING;
        } else if (byte >= 0x20 && byte <= 0x2F) {
            /* Intermediate */
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_ESCAPE_INTERMEDIATE;
        } else if (byte >= 0x30 && byte <= 0x7E) {
            /* Final byte → dispatch */
            emit_esc(p, out, (char)byte);
            p->state = WIXEN_PS_GROUND;
        } else if (byte == 0x1B) {
            /* ESC ESC — restart escape */
            clear_params(p);
            clear_intermediates(p);
        } else {
            /* Ignore or execute */
            if (byte < 0x20) emit_execute(out, byte);
            p->state = WIXEN_PS_GROUND;
        }
        break;

    case WIXEN_PS_ESCAPE_INTERMEDIATE:
        if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
        } else if (byte >= 0x30 && byte <= 0x7E) {
            emit_esc(p, out, (char)byte);
            p->state = WIXEN_PS_GROUND;
        } else {
            p->state = WIXEN_PS_GROUND;
        }
        break;

    case WIXEN_PS_CSI_ENTRY:
        if (byte >= 0x30 && byte <= 0x39) {
            /* Digit */
            p->current_param = byte - '0';
            p->has_param = true;
            p->state = WIXEN_PS_CSI_PARAM;
        } else if (byte == 0x3B) {
            /* Semicolon — empty first param */
            push_param(p);
            p->state = WIXEN_PS_CSI_PARAM;
        } else if (byte == 0x3A) {
            /* Colon — subparameter */
            p->collecting_subparam = true;
            p->state = WIXEN_PS_CSI_PARAM;
        } else if (byte >= 0x3C && byte <= 0x3F) {
            /* Private modifier (<, =, >, ?) */
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_CSI_PARAM;
        } else if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_CSI_INTERMEDIATE;
        } else if (byte >= 0x40 && byte <= 0x7E) {
            emit_csi(p, out, (char)byte);
            p->state = WIXEN_PS_GROUND;
        } else if (byte < 0x20) {
            emit_execute(out, byte);
        } else {
            p->state = WIXEN_PS_CSI_IGNORE;
        }
        break;

    case WIXEN_PS_CSI_PARAM:
        if (byte >= 0x30 && byte <= 0x39) {
            if (p->collecting_subparam) {
                p->current_subparam_value = p->current_subparam_value * 10 + (byte - '0');
                p->has_subparam_digit = true;
            } else {
                p->current_param = p->current_param * 10 + (byte - '0');
                p->has_param = true;
            }
        } else if (byte == 0x3B) {
            push_param(p);
        } else if (byte == 0x3A) {
            /* Colon: start or continue subparameters */
            if (!p->collecting_subparam) {
                /* First colon: save main param value, start subparam collection */
                p->collecting_subparam = true;
                /* current_param is the main param value — keep it */
                /* Next digits will accumulate into current_subparam_value */
                p->current_subparam_value = 0;
                p->has_subparam_digit = false;
            } else {
                /* Subsequent colon: push accumulated subparam value */
                if (p->current_subparam_count < WIXEN_MAX_SUBPARAMS) {
                    p->current_subparams[p->current_subparam_count++] =
                        p->has_subparam_digit ? p->current_subparam_value : 0;
                }
                p->current_subparam_value = 0;
                p->has_subparam_digit = false;
            }
        } else if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_CSI_INTERMEDIATE;
        } else if (byte >= 0x40 && byte <= 0x7E) {
            emit_csi(p, out, (char)byte);
            p->state = WIXEN_PS_GROUND;
        } else if (byte >= 0x3C && byte <= 0x3F) {
            /* Unexpected private modifier mid-params */
            p->state = WIXEN_PS_CSI_IGNORE;
        } else if (byte < 0x20) {
            emit_execute(out, byte);
        } else {
            p->state = WIXEN_PS_CSI_IGNORE;
        }
        break;

    case WIXEN_PS_CSI_INTERMEDIATE:
        if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
        } else if (byte >= 0x40 && byte <= 0x7E) {
            emit_csi(p, out, (char)byte);
            p->state = WIXEN_PS_GROUND;
        } else {
            p->state = WIXEN_PS_CSI_IGNORE;
        }
        break;

    case WIXEN_PS_CSI_IGNORE:
        if (byte >= 0x40 && byte <= 0x7E) {
            p->state = WIXEN_PS_GROUND;
        } else if (byte < 0x20) {
            emit_execute(out, byte);
        }
        break;

    case WIXEN_PS_OSC_STRING:
        if (byte == 0x07 || byte == 0x9C) {
            /* BEL or ST terminates OSC */
            emit_osc(p, out);
            p->state = WIXEN_PS_GROUND;
        } else if (byte == 0x1B) {
            /* Possible ESC \ (ST) */
            /* Peek ahead handled by next byte */
            /* For now, treat ESC as potential ST start */
            emit_osc(p, out);
            p->state = WIXEN_PS_ESCAPE;
        } else if (byte >= 0x20 || byte == 0x09) {
            /* Printable or tab — accumulate */
            osc_put(p, byte);
        }
        /* Ignore other control chars within OSC */
        break;

    case WIXEN_PS_DCS_ENTRY:
        if (byte >= 0x30 && byte <= 0x39) {
            p->current_param = byte - '0';
            p->has_param = true;
            p->state = WIXEN_PS_DCS_PARAM;
        } else if (byte == 0x3B) {
            push_param(p);
            p->state = WIXEN_PS_DCS_PARAM;
        } else if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_DCS_INTERMEDIATE;
        } else if (byte >= 0x40 && byte <= 0x7E) {
            /* Hook */
            finalize_params(p);
            out->type = WIXEN_ACTION_DCS_HOOK;
            memcpy(out->dcs_hook.params, p->params, sizeof(p->params));
            out->dcs_hook.param_count = p->param_count;
            memcpy(out->dcs_hook.intermediates, p->intermediates, sizeof(p->intermediates));
            out->dcs_hook.intermediate_count = p->intermediate_count;
            out->dcs_hook.final_byte = (char)byte;
            p->state = WIXEN_PS_DCS_PASSTHROUGH;
        } else if (byte >= 0x3C && byte <= 0x3F) {
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_DCS_PARAM;
        } else {
            p->state = WIXEN_PS_DCS_IGNORE;
        }
        break;

    case WIXEN_PS_DCS_PARAM:
        if (byte >= 0x30 && byte <= 0x39) {
            p->current_param = p->current_param * 10 + (byte - '0');
            p->has_param = true;
        } else if (byte == 0x3B) {
            push_param(p);
        } else if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
            p->state = WIXEN_PS_DCS_INTERMEDIATE;
        } else if (byte >= 0x40 && byte <= 0x7E) {
            finalize_params(p);
            out->type = WIXEN_ACTION_DCS_HOOK;
            memcpy(out->dcs_hook.params, p->params, sizeof(p->params));
            out->dcs_hook.param_count = p->param_count;
            memcpy(out->dcs_hook.intermediates, p->intermediates, sizeof(p->intermediates));
            out->dcs_hook.intermediate_count = p->intermediate_count;
            out->dcs_hook.final_byte = (char)byte;
            p->state = WIXEN_PS_DCS_PASSTHROUGH;
        } else {
            p->state = WIXEN_PS_DCS_IGNORE;
        }
        break;

    case WIXEN_PS_DCS_INTERMEDIATE:
        if (byte >= 0x20 && byte <= 0x2F) {
            collect_intermediate(p, byte);
        } else if (byte >= 0x40 && byte <= 0x7E) {
            finalize_params(p);
            out->type = WIXEN_ACTION_DCS_HOOK;
            memcpy(out->dcs_hook.params, p->params, sizeof(p->params));
            out->dcs_hook.param_count = p->param_count;
            memcpy(out->dcs_hook.intermediates, p->intermediates, sizeof(p->intermediates));
            out->dcs_hook.intermediate_count = p->intermediate_count;
            out->dcs_hook.final_byte = (char)byte;
            p->state = WIXEN_PS_DCS_PASSTHROUGH;
        } else {
            p->state = WIXEN_PS_DCS_IGNORE;
        }
        break;

    case WIXEN_PS_DCS_PASSTHROUGH:
        if (byte == 0x9C) {
            out->type = WIXEN_ACTION_DCS_UNHOOK;
            p->state = WIXEN_PS_GROUND;
        } else if (byte == 0x1B) {
            /* ESC — possible ST (ESC \) */
            out->type = WIXEN_ACTION_DCS_UNHOOK;
            p->state = WIXEN_PS_ESCAPE;
        } else {
            out->type = WIXEN_ACTION_DCS_PUT;
            out->dcs_byte = byte;
        }
        break;

    case WIXEN_PS_DCS_IGNORE:
        if (byte == 0x9C) {
            p->state = WIXEN_PS_GROUND;
        } else if (byte == 0x1B) {
            p->state = WIXEN_PS_ESCAPE;
        }
        break;

    case WIXEN_PS_SOS_PM_APC_STRING:
        if (byte == 0x9C) {
            p->state = WIXEN_PS_GROUND;
        } else if (byte == 0x1B) {
            p->state = WIXEN_PS_ESCAPE;
        }
        /* Ignore all other bytes */
        break;

    case WIXEN_PS_APC_STRING:
        if (byte == 0x9C) {
            emit_apc(p, out);
            p->state = WIXEN_PS_GROUND;
        } else if (byte == 0x1B) {
            emit_apc(p, out);
            p->state = WIXEN_PS_ESCAPE;
        } else {
            apc_put(p, byte);
        }
        break;
    }
}

/* --- Batch processing --- */

size_t wixen_parser_process(WixenParser *p, const uint8_t *data, size_t len,
                            WixenAction *out_actions, size_t out_cap) {
    size_t emitted = 0;
    for (size_t i = 0; i < len && emitted < out_cap; i++) {
        WixenAction action;
        wixen_parser_advance(p, data[i], &action);
        if (action.type != WIXEN_ACTION_NONE) {
            out_actions[emitted++] = action;
        }
    }
    return emitted;
}
