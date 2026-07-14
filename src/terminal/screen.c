/*
 * screen.c — the terminal screen model: cell grid + control-function semantics.
 *
 * This is where escape sequences acquire MEANING. It wires vtparse's callbacks
 * to a grid of term_cell and implements the control functions a modern TUI
 * actually uses:
 *   - printing with autowrap + wide-char (CJK/emoji) handling
 *   - cursor motion (CUP/CUU/CUD/CUF/CUB/CR/LF/BS/HT + reverse index)
 *   - SGR: reset, bold/faint/italic/underline/blink/reverse/invisible/strike,
 *          16-colour, 90-97/100-107 bright, 256-colour (38;5;n), truecolour
 *          (38;2;r;g;b), default fg/bg
 *   - erase: ED (J), EL (K) with all selectors
 *   - insert/delete: ICH/DCH/IL/DL, ECH
 *   - scroll region DECSTBM (r) + SU/SD, index/reverse-index scrolling
 *   - alternate screen (?1049 / ?47 / ?1047) with cursor save/restore
 *   - DECSC/DECRC save/restore cursor, tabs (HTS/TBC + tab stops)
 *   - autowrap (DECAWM ?7), origin mode (DECOM ?6), cursor visibility (?25)
 *
 * Colours are stored abstractly (term_color: default / indexed / rgb) so the
 * renderer resolves them through the live palette; nothing here bakes RGB for
 * indexed colours, which keeps OSC-4 palette changes and themes working later.
 */
#include "neowall/terminal/terminal.h"
#include "vtparse.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */

#define TERM_MIN_COLS 1
#define TERM_MIN_ROWS 1
#define TERM_MAX_COLS 1024
#define TERM_MAX_ROWS 512
#define TAB_WIDTH_DEFAULT 8

typedef struct {
    int        x, y;
    term_color fg, bg;
    uint16_t   attr;
    bool       origin_mode;   /* DECOM: cursor confined to scroll region */
    bool       autowrap;      /* DECAWM */
} cursor_state;

struct term_screen {
    int cols, rows;

    /* Two grids: the primary and the alternate screen. `cells` points at the
     * active one. Each is rows*cols term_cell, row-major. */
    term_cell *primary;
    term_cell *alternate;
    term_cell *cells;
    bool       on_alt;

    cursor_state cur;
    cursor_state saved_primary;   /* DECSC target while on primary */
    cursor_state saved_alt;       /* DECSC target while on alt */

    /* scroll region [top, bottom] inclusive, 0-based. */
    int scroll_top, scroll_bottom;

    /* current pen (SGR state) applied to newly printed cells */
    term_color pen_fg, pen_bg;
    uint16_t   pen_attr;

    bool cursor_visible;
    bool pending_wrap;   /* DEC "last column" deferred wrap latch */

    /* Mouse reporting state, driven by DECSET/DECRST private modes. A wallpaper
     * terminal forwards pointer events only when the app has asked for them. */
    uint16_t mouse_proto;   /* 0=off, 1000=click, 1002=drag, 1003=any-motion */
    bool    mouse_sgr;     /* 1006: SGR extended coordinates (\e[<b;x;yM/m) */

    bool *tabstops;      /* cols booleans */

    /* Charset selection (VT100 national/DEC special graphics). g[0]/g[1] hold
     * the designated set for G0/G1 ('B'=ASCII, '0'=DEC line-drawing). gl selects
     * which is active in GL (0 or 1), toggled by SI (^O) / SO (^N). */
    char g[2];
    int  gl;

    /* Latest window title from OSC 0/2 (cosmetic for a wallpaper; kept so a
     * host could surface it). NUL-terminated. */
    char title[256];

    vtparser parser;
};

/* ------------------------------------------------------------------------ */
/* grid helpers                                                             */
/* ------------------------------------------------------------------------ */

static term_cell blank_cell(const term_screen *s) {
    term_cell c = {0};
    c.cp = 0;
    c.fg = s->pen_fg.kind == TERM_COLOR_DEFAULT ? (term_color){.kind = TERM_COLOR_DEFAULT} : s->pen_fg;
    /* Erased cells take the CURRENT background (so a coloured-bg clear works),
     * but default fg. This matches xterm's "erase to background colour". */
    c.fg = (term_color){.kind = TERM_COLOR_DEFAULT};
    c.bg = s->pen_bg;
    c.attr = 0;
    return c;
}

static term_cell *cell_at(term_screen *s, int x, int y) {
    return &s->cells[(size_t)y * s->cols + x];
}

static void row_fill_blank(term_screen *s, int y, int x0, int x1) {
    term_cell b = blank_cell(s);
    for (int x = x0; x <= x1 && x < s->cols; x++) {
        if (x < 0) continue;
        s->cells[(size_t)y * s->cols + x] = b;
    }
}

static void clear_all(term_screen *s) {
    for (int y = 0; y < s->rows; y++) row_fill_blank(s, y, 0, s->cols - 1);
}

/* Scroll the region [top,bottom] up by n lines, filling from the bottom with
 * blanks. Used by LF at the region bottom and by SU. */
static void scroll_up(term_screen *s, int top, int bottom, int n) {
    if (n <= 0) return;
    if (n > bottom - top + 1) n = bottom - top + 1;
    for (int y = top; y <= bottom - n; y++) {
        memcpy(&s->cells[(size_t)y * s->cols],
               &s->cells[(size_t)(y + n) * s->cols],
               (size_t)s->cols * sizeof(term_cell));
    }
    for (int y = bottom - n + 1; y <= bottom; y++) row_fill_blank(s, y, 0, s->cols - 1);
}

static void scroll_down(term_screen *s, int top, int bottom, int n) {
    if (n <= 0) return;
    if (n > bottom - top + 1) n = bottom - top + 1;
    for (int y = bottom; y >= top + n; y--) {
        memcpy(&s->cells[(size_t)y * s->cols],
               &s->cells[(size_t)(y - n) * s->cols],
               (size_t)s->cols * sizeof(term_cell));
    }
    for (int y = top; y < top + n; y++) row_fill_blank(s, y, 0, s->cols - 1);
}

/* ------------------------------------------------------------------------ */
/* cursor + printing                                                        */
/* ------------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void cursor_to(term_screen *s, int x, int y) {
    s->cur.x = clampi(x, 0, s->cols - 1);
    if (s->cur.origin_mode) {
        s->cur.y = clampi(y, s->scroll_top, s->scroll_bottom);
    } else {
        s->cur.y = clampi(y, 0, s->rows - 1);
    }
    s->pending_wrap = false;
}

/* Advance to a fresh line, scrolling the region if at its bottom. */
static void line_feed(term_screen *s) {
    if (s->cur.y == s->scroll_bottom) {
        scroll_up(s, s->scroll_top, s->scroll_bottom, 1);
    } else if (s->cur.y < s->rows - 1) {
        s->cur.y++;
    }
    s->pending_wrap = false;
}

static void reverse_index(term_screen *s) {
    if (s->cur.y == s->scroll_top) {
        scroll_down(s, s->scroll_top, s->scroll_bottom, 1);
    } else if (s->cur.y > 0) {
        s->cur.y--;
    }
    s->pending_wrap = false;
}

static int char_width(uint32_t cp);

static void put_glyph(term_screen *s, uint32_t cp) {
    int w = char_width(cp);
    if (w == 0) {
        /* Combining mark: attach to the previous cell if possible. We keep a
         * single base codepoint per cell for now (no combining stacking yet);
         * dropping the mark is the safe degrade and doesn't shift the grid. */
        return;
    }

    if (s->pending_wrap && s->cur.autowrap) {
        s->cur.x = 0;
        line_feed(s);
        s->pending_wrap = false;
    }

    /* Wide char that would overflow the last column: wrap first. */
    if (w == 2 && s->cur.x == s->cols - 1) {
        if (s->cur.autowrap) {
            /* blank the last cell, wrap */
            term_cell b = blank_cell(s);
            *cell_at(s, s->cur.x, s->cur.y) = b;
            s->cur.x = 0;
            line_feed(s);
        } else {
            s->cur.x = s->cols - 2 >= 0 ? s->cols - 2 : 0;
        }
    }

    term_cell *c = cell_at(s, s->cur.x, s->cur.y);
    c->cp = cp;
    c->fg = s->pen_fg;
    c->bg = s->pen_bg;
    c->attr = s->pen_attr;

    if (w == 2 && s->cur.x + 1 < s->cols) {
        term_cell *tail = cell_at(s, s->cur.x + 1, s->cur.y);
        tail->cp = 0;
        tail->fg = s->pen_fg;
        tail->bg = s->pen_bg;
        tail->attr = (uint16_t)(s->pen_attr | TERM_ATTR_WIDE_TAIL);
    }

    s->cur.x += w;
    if (s->cur.x >= s->cols) {
        s->cur.x = s->cols - 1;
        s->pending_wrap = true;   /* defer the wrap until the next glyph */
    }
}

/* ------------------------------------------------------------------------ */
/* tabs                                                                     */
/* ------------------------------------------------------------------------ */

static void reset_tabstops(term_screen *s) {
    for (int x = 0; x < s->cols; x++) s->tabstops[x] = (x % TAB_WIDTH_DEFAULT) == 0 && x != 0;
    /* column 0 is not a stop; first stop at 8. */
}

static void tab_forward(term_screen *s) {
    int x = s->cur.x;
    for (int i = x + 1; i < s->cols; i++) {
        if (s->tabstops[i]) { s->cur.x = i; s->pending_wrap = false; return; }
    }
    s->cur.x = s->cols - 1;
    s->pending_wrap = false;
}

/* ------------------------------------------------------------------------ */
/* parser callbacks                                                         */
/* ------------------------------------------------------------------------ */

static void cb_print(void *u, uint32_t cp) {
    term_screen *s = u;
    /* DEC special graphics (ESC ( 0, active via SO/SI): map the ASCII range
     * 0x60..0x7E to the VT100 line-drawing Unicode codepoints so legacy apps
     * that draw boxes with `qqqq`/`lk`/`mj` render real box lines, not letters. */
    if (s->g[s->gl] == '0' && cp >= 0x60 && cp <= 0x7E) {
        static const uint32_t dec[0x7F - 0x60] = {
            0x25C6,0x2592,0x2409,0x240C,0x240D,0x240A,0x00B0,0x00B1, /* ` a b c d e f g */
            0x2424,0x240B,0x2518,0x2510,0x250C,0x2514,0x253C,0x23BA, /* h i j k l m n o */
            0x23BB,0x2500,0x23BC,0x23BD,0x251C,0x2524,0x2534,0x252C, /* p q r s t u v w */
            0x2502,0x2264,0x2265,0x03C0,0x2260,0x00A3,0x00B7,        /* x y z { | } ~ */
        };
        cp = dec[cp - 0x60];
    }
    put_glyph(s, cp);
}

static void cb_execute(void *u, uint8_t c) {
    term_screen *s = u;
    switch (c) {
        case 0x07: /* BEL */ break;
        case 0x08: /* BS  */ if (s->cur.x > 0) s->cur.x--; s->pending_wrap = false; break;
        case 0x09: /* HT  */ tab_forward(s); break;
        case 0x0A: /* LF  */
        case 0x0B: /* VT  */
        case 0x0C: /* FF  */ line_feed(s); break;
        case 0x0D: /* CR  */ s->cur.x = 0; s->pending_wrap = false; break;
        case 0x0E: /* SO (^N): shift to G1 */ s->gl = 1; break;
        case 0x0F: /* SI (^O): shift to G0 */ s->gl = 0; break;
        default: break;
    }
}

/* param with default */
static int pget(const int *p, int n, int i, int def) {
    if (i >= n) return def;
    return (p[i] < 0) ? def : p[i];
}

/* OSC handler: Operating System Commands, "<Ps>;<Pt>". We consume window title
 * (0/2) into s->title; palette / clipboard / hyperlink commands are parsed but
 * not acted on (a wallpaper has no window title bar or clipboard). */
static void cb_osc(void *u, const uint8_t *data, size_t len) {
    term_screen *s = u;
    if (!data || len == 0) return;
    /* split leading numeric Ps up to the first ';' */
    size_t i = 0;
    int ps = 0; bool have_ps = false;
    while (i < len && data[i] >= '0' && data[i] <= '9') { ps = ps * 10 + (data[i] - '0'); have_ps = true; i++; }
    if (i < len && data[i] == ';') i++;
    if (!have_ps) return;
    if (ps == 0 || ps == 2) {           /* set window/icon title */
        size_t n = len - i;
        if (n >= sizeof(s->title)) n = sizeof(s->title) - 1;
        memcpy(s->title, data + i, n);
        s->title[n] = 0;
    }
    /* ps 4/10/11 (palette / default fg-bg), 52 (clipboard), 8 (hyperlink):
     * accepted, no-op — out of scope for a wallpaper surface. */
}

static void apply_sgr(term_screen *s, const int *p, int n) {
    if (n == 0) {
        /* bare CSI m == reset */
        s->pen_fg = (term_color){.kind = TERM_COLOR_DEFAULT};
        s->pen_bg = (term_color){.kind = TERM_COLOR_DEFAULT};
        s->pen_attr = 0;
        return;
    }
    for (int i = 0; i < n; i++) {
        int v = p[i] < 0 ? 0 : p[i];
        switch (v) {
            case 0:  s->pen_fg = (term_color){.kind = TERM_COLOR_DEFAULT};
                     s->pen_bg = (term_color){.kind = TERM_COLOR_DEFAULT};
                     s->pen_attr = 0; break;
            case 1:  s->pen_attr |= TERM_ATTR_BOLD; break;
            case 2:  s->pen_attr |= TERM_ATTR_FAINT; break;
            case 3:  s->pen_attr |= TERM_ATTR_ITALIC; break;
            case 4:  s->pen_attr |= TERM_ATTR_UNDERLINE; break;
            case 5:
            case 6:  s->pen_attr |= TERM_ATTR_BLINK; break;
            case 7:  s->pen_attr |= TERM_ATTR_REVERSE; break;
            case 8:  s->pen_attr |= TERM_ATTR_INVISIBLE; break;
            case 9:  s->pen_attr |= TERM_ATTR_STRIKE; break;
            case 21:
            case 22: s->pen_attr &= ~(TERM_ATTR_BOLD | TERM_ATTR_FAINT); break;
            case 23: s->pen_attr &= ~TERM_ATTR_ITALIC; break;
            case 24: s->pen_attr &= ~TERM_ATTR_UNDERLINE; break;
            case 25: s->pen_attr &= ~TERM_ATTR_BLINK; break;
            case 27: s->pen_attr &= ~TERM_ATTR_REVERSE; break;
            case 28: s->pen_attr &= ~TERM_ATTR_INVISIBLE; break;
            case 29: s->pen_attr &= ~TERM_ATTR_STRIKE; break;
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                s->pen_fg = (term_color){.kind = TERM_COLOR_INDEXED, .idx = (uint8_t)(v - 30)}; break;
            case 38:
            case 48: {
                /* extended colour: 38;5;n  or  38;2;r;g;b (48 = bg) */
                bool is_fg = (v == 38);
                if (i + 1 < n && p[i + 1] == 5) {
                    int idx = pget(p, n, i + 2, 0);
                    term_color c = {.kind = TERM_COLOR_INDEXED, .idx = (uint8_t)clampi(idx, 0, 255)};
                    if (is_fg) s->pen_fg = c; else s->pen_bg = c;
                    i += 2;
                } else if (i + 1 < n && p[i + 1] == 2) {
                    int r = pget(p, n, i + 2, 0), g = pget(p, n, i + 3, 0), bb = pget(p, n, i + 4, 0);
                    term_color c = {.kind = TERM_COLOR_RGB, .r = (uint8_t)clampi(r,0,255),
                                    .g = (uint8_t)clampi(g,0,255), .b = (uint8_t)clampi(bb,0,255)};
                    if (is_fg) s->pen_fg = c; else s->pen_bg = c;
                    i += 4;
                }
                break;
            }
            case 39: s->pen_fg = (term_color){.kind = TERM_COLOR_DEFAULT}; break;
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                s->pen_bg = (term_color){.kind = TERM_COLOR_INDEXED, .idx = (uint8_t)(v - 40)}; break;
            case 49: s->pen_bg = (term_color){.kind = TERM_COLOR_DEFAULT}; break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                s->pen_fg = (term_color){.kind = TERM_COLOR_INDEXED, .idx = (uint8_t)(v - 90 + 8)}; break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                s->pen_bg = (term_color){.kind = TERM_COLOR_INDEXED, .idx = (uint8_t)(v - 100 + 8)}; break;
            default: break;
        }
    }
}

static void set_mode(term_screen *s, uint8_t marker, const int *p, int n, bool set);

static void enter_alt(term_screen *s, bool clear);
static void leave_alt(term_screen *s);

static void cb_csi(void *u, uint8_t final, const int *p, int n,
                   uint8_t marker, const uint8_t *im, int nim) {
    term_screen *s = u;
    (void)im; (void)nim;

    switch (final) {
        case 'A': cursor_to(s, s->cur.x, s->cur.y - pget(p,n,0,1)); break;                 /* CUU */
        case 'B': cursor_to(s, s->cur.x, s->cur.y + pget(p,n,0,1)); break;                 /* CUD */
        case 'C': cursor_to(s, s->cur.x + pget(p,n,0,1), s->cur.y); break;                 /* CUF */
        case 'D': cursor_to(s, s->cur.x - pget(p,n,0,1), s->cur.y); break;                 /* CUB */
        case 'E': cursor_to(s, 0, s->cur.y + pget(p,n,0,1)); break;                        /* CNL */
        case 'F': cursor_to(s, 0, s->cur.y - pget(p,n,0,1)); break;                        /* CPL */
        case 'G': case '`': cursor_to(s, pget(p,n,0,1) - 1, s->cur.y); break;              /* CHA / HPA */
        case 'd': cursor_to(s, s->cur.x, pget(p,n,0,1) - 1); break;                        /* VPA */
        case 'H': case 'f': {                                                              /* CUP / HVP */
            int row = pget(p,n,0,1) - 1, col = pget(p,n,1,1) - 1;
            if (s->cur.origin_mode) row += s->scroll_top;
            cursor_to(s, col, row);
            break;
        }
        case 'J': { /* ED */
            int mode = pget(p,n,0,0);
            if (mode == 0)      { row_fill_blank(s, s->cur.y, s->cur.x, s->cols - 1);
                                  for (int y = s->cur.y + 1; y < s->rows; y++) row_fill_blank(s, y, 0, s->cols - 1); }
            else if (mode == 1) { for (int y = 0; y < s->cur.y; y++) row_fill_blank(s, y, 0, s->cols - 1);
                                  row_fill_blank(s, s->cur.y, 0, s->cur.x); }
            else if (mode == 2 || mode == 3) clear_all(s);
            break;
        }
        case 'K': { /* EL */
            int mode = pget(p,n,0,0);
            if (mode == 0)      row_fill_blank(s, s->cur.y, s->cur.x, s->cols - 1);
            else if (mode == 1) row_fill_blank(s, s->cur.y, 0, s->cur.x);
            else if (mode == 2) row_fill_blank(s, s->cur.y, 0, s->cols - 1);
            break;
        }
        case 'L': scroll_down(s, s->cur.y, s->scroll_bottom, pget(p,n,0,1)); break;        /* IL */
        case 'M': scroll_up(s, s->cur.y, s->scroll_bottom, pget(p,n,0,1)); break;          /* DL */
        case 'P': { /* DCH: delete chars, shift left */
            int cnt = pget(p,n,0,1), y = s->cur.y, x = s->cur.x;
            if (cnt > s->cols - x) cnt = s->cols - x;
            for (int i = x; i < s->cols - cnt; i++) *cell_at(s,i,y) = *cell_at(s,i+cnt,y);
            row_fill_blank(s, y, s->cols - cnt, s->cols - 1);
            break;
        }
        case '@': { /* ICH: insert blanks, shift right */
            int cnt = pget(p,n,0,1), y = s->cur.y, x = s->cur.x;
            if (cnt > s->cols - x) cnt = s->cols - x;
            for (int i = s->cols - 1; i >= x + cnt; i--) *cell_at(s,i,y) = *cell_at(s,i-cnt,y);
            row_fill_blank(s, y, x, x + cnt - 1);
            break;
        }
        case 'X': { /* ECH: erase chars in place */
            int cnt = pget(p,n,0,1), y = s->cur.y, x = s->cur.x;
            row_fill_blank(s, y, x, x + cnt - 1);
            break;
        }
        case 'S': scroll_up(s, s->scroll_top, s->scroll_bottom, pget(p,n,0,1)); break;     /* SU */
        case 'T': scroll_down(s, s->scroll_top, s->scroll_bottom, pget(p,n,0,1)); break;   /* SD */
        case 'r': { /* DECSTBM: set scroll region */
            int top = pget(p,n,0,1) - 1, bot = pget(p,n,1,s->rows) - 1;
            if (top < 0) top = 0;
            if (bot > s->rows - 1) bot = s->rows - 1;
            if (top < bot) { s->scroll_top = top; s->scroll_bottom = bot;
                             cursor_to(s, 0, s->cur.origin_mode ? top : 0); }
            break;
        }
        case 'm': apply_sgr(s, p, n); break;                                               /* SGR */
        case 'h': set_mode(s, marker, p, n, true); break;                                  /* SM / DECSET */
        case 'l': set_mode(s, marker, p, n, false); break;                                 /* RM / DECRST */
        case 's': /* save cursor (ANSI.SYS) */ s->saved_primary = s->cur; break;
        case 'u': /* restore cursor (ANSI.SYS) */ s->cur = s->saved_primary; break;
        case 'g': { /* TBC */
            int mode = pget(p,n,0,0);
            if (mode == 0) s->tabstops[s->cur.x] = false;
            else if (mode == 3) for (int x = 0; x < s->cols; x++) s->tabstops[x] = false;
            break;
        }
        default: break;
    }
}

static void cb_esc(void *u, uint8_t final, const uint8_t *im, int nim) {
    term_screen *s = u;
    (void)im; (void)nim;
    if (nim == 0) {
        switch (final) {
            case 'D': line_feed(s); break;            /* IND */
            case 'M': reverse_index(s); break;        /* RI  */
            case 'E': s->cur.x = 0; line_feed(s); break; /* NEL */
            case '7': *(s->on_alt ? &s->saved_alt : &s->saved_primary) = s->cur; break; /* DECSC */
            case '8': s->cur = *(s->on_alt ? &s->saved_alt : &s->saved_primary); break; /* DECRC */
            case 'H': s->tabstops[s->cur.x] = true; break; /* HTS */
            case 'c': /* RIS: full reset */
                s->pen_fg = (term_color){.kind = TERM_COLOR_DEFAULT};
                s->pen_bg = (term_color){.kind = TERM_COLOR_DEFAULT};
                s->pen_attr = 0; s->cursor_visible = true;
                s->g[0] = 'B'; s->g[1] = 'B'; s->gl = 0;
                s->scroll_top = 0; s->scroll_bottom = s->rows - 1;
                clear_all(s); cursor_to(s, 0, 0); reset_tabstops(s);
                break;
            default: break;
        }
    }
    /* Charset designation: ESC ( <c> designates G0, ESC ) <c> designates G1.
     * <c>='0' selects the DEC special-graphics (line-drawing) set, 'B'/'0'
     * otherwise ASCII. We honour '0' vs everything-else (ASCII) which covers
     * every box-drawing TUI; other national sets fall back to ASCII. */
    if (nim >= 1 && (im[0] == '(' || im[0] == ')')) {
        int gset = (im[0] == ')') ? 1 : 0;
        s->g[gset] = (char)final;
    }
}

/* DEC private + ANSI modes. */
static void set_mode(term_screen *s, uint8_t marker, const int *p, int n, bool set) {
    for (int i = 0; i < n; i++) {
        int m = p[i];
        if (marker == '?') {
            switch (m) {
                case 6:  s->cur.origin_mode = set; cursor_to(s, 0, set ? s->scroll_top : 0); break; /* DECOM */
                case 7:  s->cur.autowrap = set; break;                                    /* DECAWM */
                case 25: s->cursor_visible = set; break;                                  /* DECTCEM */
                case 47:
                case 1047: set ? enter_alt(s, m == 1047) : leave_alt(s); break;
                case 1048: if (set) s->saved_alt = s->cur; else s->cur = s->saved_alt; break;
                case 1049:
                    if (set) { s->saved_primary = s->cur; enter_alt(s, true); }
                    else     { leave_alt(s); s->cur = s->saved_primary; }
                    break;
                /* Mouse reporting protocols. We keep the highest-fidelity one
                 * the app enabled; disabling any clears reporting. */
                case 1000: s->mouse_proto = set ? 1000 : 0; break; /* click only */
                case 1002: s->mouse_proto = set ? 1002 : 0; break; /* button-drag */
                case 1003: s->mouse_proto = set ? 1003 : 0; break; /* any-motion */
                case 1006: s->mouse_sgr = set; break;             /* SGR ext coords */
                default: break; /* bracketed paste, etc. — no-op for a wallpaper */
            }
        } else {
            switch (m) {
                case 4: /* IRM insert mode — TUIs rarely use; skip for now */ break;
                default: break;
            }
        }
    }
}

static void enter_alt(term_screen *s, bool clear) {
    if (s->on_alt) return;
    s->saved_primary = s->cur;
    s->cells = s->alternate;
    s->on_alt = true;
    if (clear) clear_all(s);
    s->pending_wrap = false;
}

static void leave_alt(term_screen *s) {
    if (!s->on_alt) return;
    s->cells = s->primary;
    s->on_alt = false;
    s->pending_wrap = false;
}

/* ------------------------------------------------------------------------ */
/* char width (very small, pragmatic wcwidth)                               */
/* ------------------------------------------------------------------------ */

/* Returns 0 (combining/zero-width), 1 (normal), or 2 (wide, e.g. CJK/emoji).
 * A compact approximation of wcwidth covering the common wide ranges. Full
 * Unicode width tables can be swapped in later without touching callers. */
static int char_width(uint32_t cp) {
    if (cp == 0) return 1;
    if (cp < 0x20) return 0;
    /* zero-width: combining marks, ZWJ, variation selectors */
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x200B && cp <= 0x200F) ||
        (cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        cp == 0x200D)
        return 0;
    /* wide ranges (CJK, Hangul, wide kana, wide emoji blocks) */
    if ((cp >= 0x1100 && cp <= 0x115F) ||   /* Hangul Jamo */
        (cp >= 0x2E80 && cp <= 0x303E) ||   /* CJK radicals..symbols */
        (cp >= 0x3041 && cp <= 0x33FF) ||   /* Hiragana..CJK compat */
        (cp >= 0x3400 && cp <= 0x4DBF) ||   /* CJK Ext A */
        (cp >= 0x4E00 && cp <= 0x9FFF) ||   /* CJK Unified */
        (cp >= 0xA000 && cp <= 0xA4CF) ||   /* Yi */
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   /* Hangul syllables */
        (cp >= 0xF900 && cp <= 0xFAFF) ||   /* CJK compat ideographs */
        (cp >= 0xFE30 && cp <= 0xFE4F) ||   /* CJK compat forms */
        (cp >= 0xFF00 && cp <= 0xFF60) ||   /* fullwidth forms */
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF) || /* emoji + symbols */
        (cp >= 0x20000 && cp <= 0x3FFFD))   /* CJK Ext B..  */
        return 2;
    return 1;
}

/* ------------------------------------------------------------------------ */
/* public API                                                               */
/* ------------------------------------------------------------------------ */

static void screen_alloc_grids(term_screen *s) {
    size_t nc = (size_t)s->cols * s->rows;
    s->primary   = calloc(nc, sizeof(term_cell));
    s->alternate = calloc(nc, sizeof(term_cell));
    s->tabstops  = calloc((size_t)s->cols, sizeof(bool));
    s->cells = s->primary;
}

term_screen *term_screen_create(int cols, int rows) {
    cols = clampi(cols, TERM_MIN_COLS, TERM_MAX_COLS);
    rows = clampi(rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
    term_screen *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->cols = cols; s->rows = rows;
    screen_alloc_grids(s);
    if (!s->primary || !s->alternate || !s->tabstops) {
        term_screen_destroy(s);
        return NULL;
    }
    s->scroll_top = 0; s->scroll_bottom = rows - 1;
    s->pen_fg = (term_color){.kind = TERM_COLOR_DEFAULT};
    s->pen_bg = (term_color){.kind = TERM_COLOR_DEFAULT};
    s->cursor_visible = true;
    s->cur.autowrap = true;
    s->g[0] = 'B'; s->g[1] = 'B'; s->gl = 0;   /* both G0/G1 = ASCII */
    reset_tabstops(s);
    clear_all(s);

    vt_callbacks cb = {0};
    cb.print = cb_print;
    cb.execute = cb_execute;
    cb.csi = cb_csi;
    cb.esc = cb_esc;
    cb.osc = cb_osc;
    vtparse_init(&s->parser, &cb, s);
    return s;
}

void term_screen_destroy(term_screen *s) {
    if (!s) return;
    free(s->primary);
    free(s->alternate);
    free(s->tabstops);
    free(s);
}

void term_screen_feed(term_screen *s, const uint8_t *bytes, size_t len) {
    vtparse_feed(&s->parser, bytes, len);
}

int  term_screen_cols(const term_screen *s) { return s->cols; }
int  term_screen_rows(const term_screen *s) { return s->rows; }

const term_cell *term_screen_row(const term_screen *s, int y) {
    if (y < 0 || y >= s->rows) return NULL;
    return &s->cells[(size_t)y * s->cols];
}

void term_screen_cursor(const term_screen *s, int *x, int *y) {
    if (x) *x = s->cur.x;
    if (y) *y = s->cur.y;
}

bool term_screen_cursor_visible(const term_screen *s) {
    return s ? s->cursor_visible : true;
}

const char *term_screen_title(const term_screen *s) {
    return s ? s->title : "";
}

void term_screen_mouse_mode(const term_screen *s, int *proto, bool *sgr) {
    if (proto) *proto = s ? s->mouse_proto : 0;
    if (sgr)   *sgr   = s ? s->mouse_sgr : false;
}

/* Resize: reallocate grids, preserving the top-left overlap of the primary
 * screen. Simple and correct; full reflow (rewrapping soft-wrapped lines) is a
 * later refinement. */
void term_screen_resize(term_screen *s, int cols, int rows) {
    cols = clampi(cols, TERM_MIN_COLS, TERM_MAX_COLS);
    rows = clampi(rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
    if (cols == s->cols && rows == s->rows) return;

    term_cell *np = calloc((size_t)cols * rows, sizeof(term_cell));
    term_cell *na = calloc((size_t)cols * rows, sizeof(term_cell));
    bool *nt = calloc((size_t)cols, sizeof(bool));
    if (!np || !na || !nt) { free(np); free(na); free(nt); return; }

    int copy_rows = rows < s->rows ? rows : s->rows;
    int copy_cols = cols < s->cols ? cols : s->cols;
    for (int y = 0; y < copy_rows; y++) {
        memcpy(&np[(size_t)y * cols], &s->primary[(size_t)y * s->cols],   (size_t)copy_cols * sizeof(term_cell));
        memcpy(&na[(size_t)y * cols], &s->alternate[(size_t)y * s->cols], (size_t)copy_cols * sizeof(term_cell));
    }
    bool was_alt = s->on_alt;
    free(s->primary); free(s->alternate); free(s->tabstops);
    s->primary = np; s->alternate = na; s->tabstops = nt;
    s->cols = cols; s->rows = rows;
    s->cells = was_alt ? s->alternate : s->primary;
    s->scroll_top = 0; s->scroll_bottom = rows - 1;
    s->cur.x = clampi(s->cur.x, 0, cols - 1);
    s->cur.y = clampi(s->cur.y, 0, rows - 1);
    reset_tabstops(s);
}

char *term_screen_dump_text(const term_screen *s) {
    /* Worst case: every cell a 4-byte UTF-8 char + newline per row + NUL. */
    size_t cap = (size_t)s->rows * (s->cols * 4 + 1) + 1;
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t o = 0;
    for (int y = 0; y < s->rows; y++) {
        const term_cell *row = &s->cells[(size_t)y * s->cols];
        int last = -1;
        for (int x = 0; x < s->cols; x++) if (row[x].cp != 0) last = x;
        for (int x = 0; x <= last; x++) {
            uint32_t cp = row[x].cp;
            if (row[x].attr & TERM_ATTR_WIDE_TAIL) continue;
            if (cp == 0) { out[o++] = ' '; continue; }
            /* encode UTF-8 */
            if (cp < 0x80) out[o++] = (char)cp;
            else if (cp < 0x800) { out[o++] = (char)(0xC0|(cp>>6)); out[o++] = (char)(0x80|(cp&0x3F)); }
            else if (cp < 0x10000) { out[o++] = (char)(0xE0|(cp>>12)); out[o++] = (char)(0x80|((cp>>6)&0x3F)); out[o++] = (char)(0x80|(cp&0x3F)); }
            else { out[o++] = (char)(0xF0|(cp>>18)); out[o++] = (char)(0x80|((cp>>12)&0x3F)); out[o++] = (char)(0x80|((cp>>6)&0x3F)); out[o++] = (char)(0x80|(cp&0x3F)); }
        }
        out[o++] = '\n';
    }
    out[o] = 0;
    return out;
}
