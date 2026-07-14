/*
 * vtparse.h — the ANSI/DEC escape-sequence state machine (private)
 *
 * This is the byte-exact parser at the core of any real terminal: the state
 * machine published by Paul Williams (vt100.net/emu/dec_ansi_parser), which is
 * what xterm, vte, kitty and libvterm all implement. It is a pure syntactic
 * layer — it recognises the SHAPE of control sequences (CSI / OSC / DCS / ESC /
 * plain print) and hands finished sequences to a callback vtable. It knows
 * nothing about what any sequence MEANS; screen.c gives them meaning.
 *
 * Separating "parse the bytes" from "interpret the command" is what keeps the
 * whole thing testable and correct: the state machine is a fixed, well-specified
 * automaton we can validate byte-for-byte against the reference diagram, and the
 * semantics live in a separate, independently-testable layer.
 *
 * The parser is fed one byte at a time (bytes, not codepoints — escape
 * sequences are pure ASCII/C0/C1; UTF-8 only appears in the "print" ground
 * state and is decoded there). It is fully resumable across feeds.
 */
#ifndef NEOWALL_TERMINAL_VTPARSE_H
#define NEOWALL_TERMINAL_VTPARSE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "utf8.h"

/* Limits sized to comfortably exceed anything real programs emit. Extra
 * parameters/intermediates beyond these are dropped (per the spec's behaviour
 * of a bounded parser), never overflowed. */
#define VT_MAX_PARAMS       32
#define VT_MAX_INTERMEDIATE 4
#define VT_MAX_OSC          4096   /* OSC string payload cap (title/colour/etc.) */

/* Parser states — the Williams automaton. */
typedef enum vt_state {
    VT_GROUND = 0,
    VT_ESCAPE,
    VT_ESCAPE_INTERMEDIATE,
    VT_CSI_ENTRY,
    VT_CSI_PARAM,
    VT_CSI_INTERMEDIATE,
    VT_CSI_IGNORE,
    VT_OSC_STRING,
    VT_DCS_ENTRY,
    VT_DCS_PARAM,
    VT_DCS_INTERMEDIATE,
    VT_DCS_PASSTHROUGH,
    VT_DCS_IGNORE,
    VT_SOS_PM_APC_STRING,
} vt_state;

/* Callbacks the parser invokes. All are optional (NULL = ignore). `u` is the
 * user pointer passed to vtparse_init. Parameters use -1 for "omitted/default"
 * so a handler can apply its own default (e.g. CUP with no args → 1;1). */
typedef struct vt_callbacks {
    /* A printable Unicode codepoint reached the ground state. */
    void (*print)(void *u, uint32_t cp);

    /* A C0/C1 execute control (BEL, BS, HT, LF, CR, ...). `ctrl` is the byte. */
    void (*execute)(void *u, uint8_t ctrl);

    /* A complete CSI sequence: 'final' byte, params[], and any private-marker /
     * intermediate bytes. `nparams` may be 0. `private_marker` is the '<' '=' '>'
     * '?' prefix if present, else 0. */
    void (*csi)(void *u, uint8_t final, const int *params, int nparams,
                uint8_t private_marker, const uint8_t *intermediates, int nintermediate);

    /* A complete ESC sequence that is NOT CSI/OSC/DCS: intermediates + final. */
    void (*esc)(void *u, uint8_t final, const uint8_t *intermediates, int nintermediate);

    /* A complete OSC string (already NUL-terminated in `data`, length `len`). */
    void (*osc)(void *u, const uint8_t *data, size_t len);

    /* DCS hook lifecycle (for sixel, DECRQSS replies, etc.). start gives the
     * params+final; put streams the payload bytes; unhook ends it. Optional —
     * left unimplemented until we add DCS-consuming features. */
    void (*dcs_start)(void *u, uint8_t final, const int *params, int nparams,
                      uint8_t private_marker, const uint8_t *intermediates, int nintermediate);
    void (*dcs_put)(void *u, uint8_t byte);
    void (*dcs_unhook)(void *u);
} vt_callbacks;

typedef struct vtparser {
    vt_state state;

    int      params[VT_MAX_PARAMS];
    int      nparams;
    bool     param_started;      /* have we seen a digit for the current param? */
    bool     params_overflow;    /* too many params → CSI_IGNORE */

    uint8_t  intermediates[VT_MAX_INTERMEDIATE];
    int      nintermediate;
    bool     inter_overflow;

    uint8_t  private_marker;     /* '<' '=' '>' '?' or 0 */

    uint8_t  osc[VT_MAX_OSC];
    size_t   osc_len;
    bool     osc_overflow;

    utf8_decoder utf8;           /* ground-state UTF-8 decode (bytes may split) */

    /* When ESC is seen inside an OSC/DCS/SOS string, the ESC path must be able
     * to recognise a following '\' as ST and terminate the string. This records
     * which string we were in so the ESC handler can finish it correctly. */
    vt_state string_return;

    vt_callbacks cb;
    void        *user;
} vtparser;

void vtparse_init(vtparser *p, const vt_callbacks *cb, void *user);
void vtparse_reset(vtparser *p);

/* Feed a run of raw bytes. Fully resumable: a sequence split across calls is
 * handled. */
void vtparse_feed(vtparser *p, const uint8_t *bytes, size_t len);

#endif /* NEOWALL_TERMINAL_VTPARSE_H */
