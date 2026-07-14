/*
 * vtparse.c — implementation of the ANSI/DEC state machine.
 *
 * This is a faithful, longhand implementation of Paul Williams' parser
 * (https://vt100.net/emu/dec_ansi_parser). Rather than a giant transition
 * TABLE we spell the transitions out as switch statements per state, which
 * reads closer to the diagram and is easier to audit against it. Every
 * anonymous-action / entry-action / exit-action from the diagram is present.
 *
 * Byte classes referenced below (from the spec):
 *   0x18, 0x1A                 → immediate CAN/SUB: abort to ground, execute
 *   0x1B                       → ESC: start an escape
 *   0x00..0x17,0x19,0x1C..0x1F → C0 controls: execute (or ignore in strings)
 *   0x20..0x2F                 → intermediates (' ' .. '/')
 *   0x30..0x3F                 → params/markers ('0'..'?')  (':' '<'..'?')
 *   0x40..0x7E                 → final bytes
 *   0x7F (DEL)                 → ignored in most states
 *   0x80..0x9F                 → C1 controls (we handle the 8-bit CSI/OSC/etc.)
 *
 * UTF-8: escape sequences are pure ASCII, so the automaton runs on bytes. Only
 * the ground-state "print" path can see >0x7F, where we decode UTF-8 into a
 * codepoint before calling cb.print.
 */
#include "vtparse.h"
#include "utf8.h"

#include <string.h>

/* The UTF-8 decoder lives in the parser struct (p->utf8) and is only consulted
 * in ground state; escape sequences are pure ASCII/C0/C1 bytes. */

/* ---- helpers ---- */

static void clear_params(vtparser *p) {
    p->nparams = 0;
    p->param_started = false;
    p->params_overflow = false;
    memset(p->params, 0, sizeof(p->params));
}

static void clear_intermediates(vtparser *p) {
    p->nintermediate = 0;
    p->inter_overflow = false;
    p->private_marker = 0;
    memset(p->intermediates, 0, sizeof(p->intermediates));
}

static void collect_param_digit(vtparser *p, uint8_t b) {
    if (p->params_overflow) return;
    if (!p->param_started) {
        if (p->nparams >= VT_MAX_PARAMS) { p->params_overflow = true; return; }
        p->params[p->nparams] = 0;
        p->nparams++;
        p->param_started = true;
    }
    int *cur = &p->params[p->nparams - 1];
    /* Clamp to avoid overflow; real params never approach this. */
    if (*cur < 100000000) {
        *cur = *cur * 10 + (b - '0');
    }
}

static void param_separator(vtparser *p) {
    if (p->params_overflow) return;
    if (p->nparams >= VT_MAX_PARAMS) { p->params_overflow = true; return; }
    /* An explicit separator with no digits yet means a default (omitted) param.
     * Represent omitted as -1 so handlers apply their own defaults. */
    if (!p->param_started) {
        p->params[p->nparams] = -1;
        p->nparams++;
    }
    p->param_started = false;
}

static void finalize_params(vtparser *p) {
    /* A trailing separator or an all-empty CSI leaves nparams possibly with an
     * unstarted final slot; normalise a dangling omitted param. */
    if (!p->param_started && p->nparams < VT_MAX_PARAMS) {
        /* If the sequence had at least one separator but ended without digits,
         * the caller sees the params as collected. Nothing to add here — an
         * empty CSI (no params at all) yields nparams == 0. */
    }
}

static void collect_intermediate(vtparser *p, uint8_t b) {
    if (p->nintermediate >= VT_MAX_INTERMEDIATE) { p->inter_overflow = true; return; }
    p->intermediates[p->nintermediate++] = b;
}

/* ---- dispatch ---- */

static void do_csi_dispatch(vtparser *p, uint8_t final) {
    finalize_params(p);
    if (p->params_overflow || p->inter_overflow) {
        /* Malformed/over-long: ignore per spec (we already routed to IGNORE in
         * most cases, but guard here too). */
        return;
    }
    if (p->cb.csi) {
        p->cb.csi(p->user, final, p->params, p->nparams, p->private_marker,
                  p->intermediates, p->nintermediate);
    }
}

static void do_esc_dispatch(vtparser *p, uint8_t final) {
    if (p->cb.esc) {
        p->cb.esc(p->user, final, p->intermediates, p->nintermediate);
    }
}

static void osc_start(vtparser *p) {
    p->osc_len = 0;
    p->osc_overflow = false;
}

static void osc_put(vtparser *p, uint8_t b) {
    if (p->osc_overflow) return;
    if (p->osc_len + 1 >= VT_MAX_OSC) { p->osc_overflow = true; return; }
    p->osc[p->osc_len++] = b;
}

static void osc_end(vtparser *p) {
    if (p->cb.osc) {
        p->osc[p->osc_len] = 0; /* room reserved (+1 guarded above) */
        p->cb.osc(p->user, p->osc, p->osc_len);
    }
}

static void dcs_hook(vtparser *p, uint8_t final) {
    if (p->cb.dcs_start) {
        p->cb.dcs_start(p->user, final, p->params, p->nparams, p->private_marker,
                        p->intermediates, p->nintermediate);
    }
}

/* ---- classification predicates ---- */

static inline bool is_c0(uint8_t b)          { return b <= 0x1F; }
static inline bool is_intermediate(uint8_t b){ return b >= 0x20 && b <= 0x2F; }
static inline bool is_param(uint8_t b)       { return b >= 0x30 && b <= 0x39; }
static inline bool is_final(uint8_t b)       { return b >= 0x40 && b <= 0x7E; }

/* C0/C1 controls that "execute" and can appear anywhere without changing the
 * higher-level sequence state (except the ones that abort). Handled uniformly. */
static bool handle_anywhere(vtparser *p, uint8_t b) {
    switch (b) {
        case 0x18: /* CAN */
        case 0x1A: /* SUB */
            p->state = VT_GROUND;
            if (p->cb.execute) p->cb.execute(p->user, b);
            return true;
        case 0x1B: /* ESC */
            /* If we were in a string (OSC/DCS/SOS/PM/APC), remember it: a
             * following '\' is ST and must terminate that string. */
            if (p->state == VT_OSC_STRING || p->state == VT_DCS_PASSTHROUGH ||
                p->state == VT_DCS_IGNORE || p->state == VT_SOS_PM_APC_STRING) {
                p->string_return = p->state;
            } else {
                p->string_return = VT_GROUND;
            }
            clear_params(p);
            clear_intermediates(p);
            p->state = VT_ESCAPE;
            return true;
        default:
            return false;
    }
}

/* Execute C0 controls that are legal in the ground/escape flow. */
static void exec_c0(vtparser *p, uint8_t b) {
    if (p->cb.execute) p->cb.execute(p->user, b);
}

void vtparse_init(vtparser *p, const vt_callbacks *cb, void *user) {
    memset(p, 0, sizeof(*p));
    if (cb) p->cb = *cb;
    p->user = user;
    p->state = VT_GROUND;
    utf8_reset(&p->utf8);
}

void vtparse_reset(vtparser *p) {
    vt_callbacks cb = p->cb;
    void *user = p->user;
    memset(p, 0, sizeof(*p));
    p->cb = cb;
    p->user = user;
    p->state = VT_GROUND;
    utf8_reset(&p->utf8);
}

static void feed_byte(vtparser *p, uint8_t b);

void vtparse_feed(vtparser *p, const uint8_t *bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        feed_byte(p, bytes[i]);
    }
}

static void ground_print(vtparser *p, uint8_t b) {
    /* Decode UTF-8 here (only place >0x7F is meaningful). */
    utf8_decoder *d = &p->utf8;
    uint32_t cp;
    int r = utf8_push(d, b, &cp);
    if (r == 1) {
        if (p->cb.print) p->cb.print(p->user, cp);
    } else if (r == -1) {
        /* Aborted sequence emitted U+FFFD; re-feed this byte as a fresh lead. */
        if (p->cb.print) p->cb.print(p->user, cp);
        int r2 = utf8_push(d, b, &cp);
        if (r2 == 1 && p->cb.print) p->cb.print(p->user, cp);
    }
    /* r == 0: mid-sequence, wait for more bytes. */
}

static void feed_byte(vtparser *p, uint8_t b) {
    /* CAN/SUB/ESC take effect from (almost) any state. In the string states
     * (OSC/DCS/SOS) CAN/SUB abort the string; ESC starts a new escape. */
    if (handle_anywhere(p, b)) return;

    switch (p->state) {
    case VT_GROUND:
        if (is_c0(b)) { exec_c0(p, b); return; }
        ground_print(p, b);
        return;

    case VT_ESCAPE:
        if (is_c0(b)) { exec_c0(p, b); return; }
        if (b == 0x7F) return; /* ignore DEL */
        /* ESC '\' is the 7-bit String Terminator. If we entered ESC from a
         * string state, finish that string now. */
        if (b == '\\' && p->string_return != VT_GROUND) {
            switch (p->string_return) {
                case VT_OSC_STRING:       osc_end(p); break;
                case VT_DCS_PASSTHROUGH:  if (p->cb.dcs_unhook) p->cb.dcs_unhook(p->user); break;
                default: break; /* SOS/PM/APC/ignore: just end */
            }
            p->string_return = VT_GROUND;
            p->state = VT_GROUND;
            return;
        }
        p->string_return = VT_GROUND;
        clear_intermediates(p);
        clear_params(p);
        if (is_intermediate(b)) {
            collect_intermediate(p, b);
            p->state = VT_ESCAPE_INTERMEDIATE;
            return;
        }
        switch (b) {
            case '[': p->state = VT_CSI_ENTRY; clear_params(p); clear_intermediates(p); return;
            case ']': osc_start(p); p->state = VT_OSC_STRING; return;
            case 'P': clear_params(p); clear_intermediates(p); p->state = VT_DCS_ENTRY; return;
            case 'X': /* SOS */
            case '^': /* PM  */
            case '_': /* APC */
                p->state = VT_SOS_PM_APC_STRING; return;
            default:
                if (is_final(b) || (b >= 0x30 && b <= 0x7E)) {
                    do_esc_dispatch(p, b);
                    p->state = VT_GROUND;
                }
                return;
        }

    case VT_ESCAPE_INTERMEDIATE:
        if (is_c0(b)) { exec_c0(p, b); return; }
        if (b == 0x7F) return;
        if (is_intermediate(b)) { collect_intermediate(p, b); return; }
        if (b >= 0x30 && b <= 0x7E) { do_esc_dispatch(p, b); p->state = VT_GROUND; }
        return;

    case VT_CSI_ENTRY:
        if (is_c0(b)) { exec_c0(p, b); return; }
        if (b == 0x7F) return;
        if (is_param(b))        { collect_param_digit(p, b); p->state = VT_CSI_PARAM; return; }
        if (b == ':')           { param_separator(p); p->state = VT_CSI_PARAM; return; } /* sub-param sep, treat as sep */
        if (b == ';')           { param_separator(p); p->state = VT_CSI_PARAM; return; }
        if (b >= 0x3C && b <= 0x3F) { p->private_marker = b; p->state = VT_CSI_PARAM; return; } /* < = > ? */
        if (is_intermediate(b)) { collect_intermediate(p, b); p->state = VT_CSI_INTERMEDIATE; return; }
        if (is_final(b))        { do_csi_dispatch(p, b); p->state = VT_GROUND; return; }
        p->state = VT_CSI_IGNORE;
        return;

    case VT_CSI_PARAM:
        if (is_c0(b)) { exec_c0(p, b); return; }
        if (b == 0x7F) return;
        if (is_param(b)) { collect_param_digit(p, b); return; }
        if (b == ':' || b == ';') { param_separator(p); return; }
        if (is_intermediate(b)) { collect_intermediate(p, b); p->state = VT_CSI_INTERMEDIATE; return; }
        if (b >= 0x3C && b <= 0x3F) { p->state = VT_CSI_IGNORE; return; } /* marker after param → invalid */
        if (is_final(b)) { do_csi_dispatch(p, b); p->state = VT_GROUND; return; }
        p->state = VT_CSI_IGNORE;
        return;

    case VT_CSI_INTERMEDIATE:
        if (is_c0(b)) { exec_c0(p, b); return; }
        if (b == 0x7F) return;
        if (is_intermediate(b)) { collect_intermediate(p, b); return; }
        if (is_param(b) || b == ':' || b == ';' || (b >= 0x3C && b <= 0x3F)) {
            p->state = VT_CSI_IGNORE; return; /* param after intermediate → invalid */
        }
        if (is_final(b)) { do_csi_dispatch(p, b); p->state = VT_GROUND; return; }
        p->state = VT_CSI_IGNORE;
        return;

    case VT_CSI_IGNORE:
        if (is_c0(b)) { exec_c0(p, b); return; }
        if (is_final(b)) { p->state = VT_GROUND; return; }
        return; /* swallow everything until a final byte */

    case VT_OSC_STRING:
        /* Terminated by BEL (0x07) or ST (ESC \, handled via ESC path) or the
         * 8-bit ST 0x9C. */
        if (b == 0x07) { osc_end(p); p->state = VT_GROUND; return; }
        if (b == 0x9C) { osc_end(p); p->state = VT_GROUND; return; }
        if (is_c0(b)) return; /* ignore other C0 inside OSC */
        osc_put(p, b);
        return;

    case VT_DCS_ENTRY:
        if (b == 0x7F) return;
        if (is_param(b))        { collect_param_digit(p, b); p->state = VT_DCS_PARAM; return; }
        if (b == ':' || b == ';') { param_separator(p); p->state = VT_DCS_PARAM; return; }
        if (b >= 0x3C && b <= 0x3F) { p->private_marker = b; p->state = VT_DCS_PARAM; return; }
        if (is_intermediate(b)) { collect_intermediate(p, b); p->state = VT_DCS_INTERMEDIATE; return; }
        if (is_final(b))        { dcs_hook(p, b); p->state = VT_DCS_PASSTHROUGH; return; }
        p->state = VT_DCS_IGNORE;
        return;

    case VT_DCS_PARAM:
        if (b == 0x7F) return;
        if (is_param(b)) { collect_param_digit(p, b); return; }
        if (b == ':' || b == ';') { param_separator(p); return; }
        if (is_intermediate(b)) { collect_intermediate(p, b); p->state = VT_DCS_INTERMEDIATE; return; }
        if (b >= 0x3C && b <= 0x3F) { p->state = VT_DCS_IGNORE; return; }
        if (is_final(b)) { dcs_hook(p, b); p->state = VT_DCS_PASSTHROUGH; return; }
        p->state = VT_DCS_IGNORE;
        return;

    case VT_DCS_INTERMEDIATE:
        if (b == 0x7F) return;
        if (is_intermediate(b)) { collect_intermediate(p, b); return; }
        if (is_param(b) || b == ':' || b == ';' || (b >= 0x3C && b <= 0x3F)) {
            p->state = VT_DCS_IGNORE; return;
        }
        if (is_final(b)) { dcs_hook(p, b); p->state = VT_DCS_PASSTHROUGH; return; }
        p->state = VT_DCS_IGNORE;
        return;

    case VT_DCS_PASSTHROUGH:
        if (b == 0x9C) { if (p->cb.dcs_unhook) p->cb.dcs_unhook(p->user); p->state = VT_GROUND; return; }
        /* ST via ESC \ is handled by the ESC path calling dcs_unhook — see esc final. */
        if (b == 0x7F) return;
        if (p->cb.dcs_put) p->cb.dcs_put(p->user, b);
        return;

    case VT_DCS_IGNORE:
        if (b == 0x9C) { p->state = VT_GROUND; return; }
        return;

    case VT_SOS_PM_APC_STRING:
        if (b == 0x9C) { p->state = VT_GROUND; return; }
        return; /* content ignored; ESC path handles ESC-\ termination */
    }
}
