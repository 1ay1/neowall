/*
 * neowall/terminal/terminal.h — an in-tree VT/xterm-class terminal emulator
 * ============================================================================
 *
 * This is neowall's own terminal emulator: a real PTY running an arbitrary
 * command, a spec-correct VT500-series parser (the Paul Williams ANSI/DEC state
 * machine), and a cell grid with cursor, SGR colour/attributes, scroll regions
 * and an alternate screen — everything a modern TUI (htop, btop, vim, tmux)
 * relies on. NO third-party dependency: the PTY comes from glibc (<pty.h>), and
 * every byte of the parser and screen model is our own, in the same spirit as
 * neowall's hand-rolled event loop and VIBE config parser.
 *
 * It exists so a terminal — any terminal program — can BE the wallpaper: the
 * cell grid is snapshotted, packed into a texture, and rendered by the shader
 * engine (crisp, or through a CRT/glow styling pass).
 *
 * Layering (each file is independently unit-tested, headless, no GPU):
 *   utf8.c    — incremental UTF-8 decode (bytes may split across PTY reads)
 *   vtparse.c — the byte-exact ANSI/DEC state machine (ground/esc/csi/osc/dcs)
 *   screen.c  — the cell grid + all the control-function semantics
 *   pty.c     — forkpty spawn, resize (TIOCSWINSZ), non-blocking drain, thread
 *
 * Threading: a terminal owns a reader thread that drains the PTY and feeds the
 * parser under the terminal's own mutex. The GL thread calls term_snapshot()
 * to copy a frame-coherent view of the grid. Cooperative cancel + join on
 * destroy — never pthread_cancel (the parser is not async-cancel-safe), exactly
 * as the slideshow preload thread is handled elsewhere in the tree.
 */
#ifndef NEOWALL_TERMINAL_TERMINAL_H
#define NEOWALL_TERMINAL_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "neowall/result.h"

/* ------------------------------------------------------------------------ */
/* Cell model                                                               */
/* ------------------------------------------------------------------------ */

/* Per-cell attribute bits. A cell's visual style is these flags plus an fg and
 * bg colour. Kept in one byte-ish field; extend as parity grows. */
typedef enum term_attr {
    TERM_ATTR_NONE      = 0,
    TERM_ATTR_BOLD      = 1u << 0,
    TERM_ATTR_FAINT     = 1u << 1,
    TERM_ATTR_ITALIC    = 1u << 2,
    TERM_ATTR_UNDERLINE = 1u << 3,
    TERM_ATTR_BLINK     = 1u << 4,
    TERM_ATTR_REVERSE   = 1u << 5,
    TERM_ATTR_INVISIBLE = 1u << 6,
    TERM_ATTR_STRIKE    = 1u << 7,
    /* wide-char continuation: the right half of a double-width glyph. The left
     * half carries the codepoint; this cell is a placeholder and must not be
     * drawn on its own. */
    TERM_ATTR_WIDE_TAIL = 1u << 8,
} term_attr;

/* A colour is either a palette index (0..255) or a 24-bit truecolour value.
 * We tag the discriminant so the renderer can resolve indices through the
 * live palette (which themes/OSC-4 can mutate) while truecolour is literal.
 * TERM_COLOR_DEFAULT means "use the surface's default fg/bg". */
typedef enum term_color_kind {
    TERM_COLOR_DEFAULT = 0, /* inherit the default fg/bg */
    TERM_COLOR_INDEXED,     /* .idx is a 0..255 palette slot */
    TERM_COLOR_RGB,         /* .r/.g/.b are literal 8-bit channels */
} term_color_kind;

typedef struct term_color {
    term_color_kind kind;
    uint8_t idx;          /* valid when kind == TERM_COLOR_INDEXED */
    uint8_t r, g, b;      /* valid when kind == TERM_COLOR_RGB */
} term_color;

/* One character cell. `cp` is a Unicode scalar value (0 = blank). */
typedef struct term_cell {
    uint32_t   cp;        /* Unicode codepoint; 0 == empty cell */
    term_color fg;
    term_color bg;
    uint16_t   attr;      /* OR of term_attr bits */
} term_cell;

/* ------------------------------------------------------------------------ */
/* Opaque handles                                                           */
/* ------------------------------------------------------------------------ */

/* The full emulator: PTY + parser + screen + reader thread. */
typedef struct terminal terminal;

/* The screen model alone (grid + cursor + parser feed), with no PTY/thread.
 * Exposed so the state machine and screen semantics can be unit-tested by
 * feeding bytes directly — the headless correctness harness drives this. */
typedef struct term_screen term_screen;

/* ------------------------------------------------------------------------ */
/* Spawn / lifecycle (the full emulator)                                    */
/* ------------------------------------------------------------------------ */

typedef struct term_spawn_opts {
    const char *cmd;          /* command line to run under the PTY (via $SHELL -c if it has spaces, else execvp). Required. */
    int         cols;         /* initial grid width  (clamped to sane range) */
    int         rows;         /* initial grid height */
    int         scrollback;   /* scrollback lines to retain (0 = none) */
    const char *term_env;     /* value for TERM (default "xterm-256color") */
    const char *cwd;          /* working directory for the child (NULL = inherit) */
} term_spawn_opts;

/* Spawn `opts.cmd` under a fresh PTY and start the reader thread. On success
 * *out receives an owned terminal; free it with term_destroy(). */
nw_result term_spawn(const term_spawn_opts *opts, terminal **out);

/* Stop the reader thread (cooperative), reap the child, free everything. */
void term_destroy(terminal *t);

/* Resize the grid and the PTY window (TIOCSWINSZ → child gets SIGWINCH). */
nw_result term_resize(terminal *t, int cols, int rows);

/* ------------------------------------------------------------------------ */
/* Input (host → child): write to the PTY master                            */
/* ------------------------------------------------------------------------ */

/* Write raw bytes to the PTY master (keystrokes, paste, mouse sequences).
 * Thread-safe against the reader thread. Short writes are retried. */
nw_result term_write(terminal *t, const void *bytes, size_t len);

/* A pointer event to be translated into a mouse-report sequence and written to
 * the child — but only if the app enabled mouse reporting (DECSET 1000/1002/
 * 1003). Coordinates are 0-based CELL positions. `button`: 0=left,1=middle,
 * 2=right, 3=release (X10), 64=wheel-up, 65=wheel-down. `pressed` is false for
 * a button release, true for press/motion. Returns true if a sequence was sent
 * (i.e. the app wanted it), false if mouse reporting is off or the event is not
 * relevant to the active protocol (e.g. bare motion under click-only mode). */
bool term_mouse(terminal *t, int cell_x, int cell_y, int button, bool pressed, bool motion);

/* True when the child has any mouse reporting enabled — lets the host avoid the
 * work of tracking/encoding motion when nobody is listening. */
bool term_wants_mouse(const terminal *t);

/* True once the child has exited (drives "restart the wallpaper command"). */
bool term_child_exited(const terminal *t, int *exit_status_out);

/* ------------------------------------------------------------------------ */
/* Snapshot (GL thread reads this)                                          */
/* ------------------------------------------------------------------------ */

typedef struct term_frame {
    int              cols, rows;
    const term_cell *cells;    /* rows*cols, row-major, borrowed (valid until next snapshot) */
    int              cursor_x, cursor_y;
    bool             cursor_visible;
    uint64_t         epoch;    /* bumps whenever the grid changed; skip re-upload if unchanged */
} term_frame;

/* Copy a frame-coherent view of the grid into the terminal's snapshot buffer
 * and return a borrowed pointer to it. Cheap; call once per rendered frame.
 * The returned cells pointer is valid until the next term_snapshot() call. */
const term_frame *term_snapshot(terminal *t);

/* ------------------------------------------------------------------------ */
/* Screen model (headless, no PTY) — for tests and direct feeding           */
/* ------------------------------------------------------------------------ */

term_screen *term_screen_create(int cols, int rows);
void         term_screen_destroy(term_screen *s);
void         term_screen_resize(term_screen *s, int cols, int rows);

/* Feed raw bytes (possibly a partial escape sequence) into the parser. State
 * carries across calls, so a sequence split across two feeds is handled. */
void         term_screen_feed(term_screen *s, const uint8_t *bytes, size_t len);

/* Grid accessors for tests / rendering. */
int              term_screen_cols(const term_screen *s);
int              term_screen_rows(const term_screen *s);
const term_cell *term_screen_row(const term_screen *s, int y);   /* cols cells */
void             term_screen_cursor(const term_screen *s, int *x, int *y);

/* Cursor visibility (DECTCEM). False when the app has hidden the cursor. */
bool             term_screen_cursor_visible(const term_screen *s);

/* Current mouse-reporting state (set by the app via DECSET 1000/1002/1003 and
 * 1006). *proto is 0 (off), 1000, 1002 or 1003; *sgr is true when SGR extended
 * coordinates were requested. Used to decide whether/how to forward pointer
 * events into the PTY. */
void             term_screen_mouse_mode(const term_screen *s, int *proto, bool *sgr);

/* Render the visible grid to a UTF-8 string (glyphs only, no colour), one line
 * per row, trailing blanks trimmed. Caller frees. For the headless harness so
 * we can diff our grid against what a reference terminal would show. */
char *term_screen_dump_text(const term_screen *s);

#endif /* NEOWALL_TERMINAL_TERMINAL_H */
