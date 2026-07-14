/*
 * test_terminal_screen.c — headless correctness tests for the VT screen model.
 *
 * Drives term_screen directly (no PTY, no GPU) by feeding raw escape-sequence
 * byte streams and asserting the resulting cell grid. Covers the control
 * functions that real TUIs (htop, btop, vim, tmux) depend on: cursor motion,
 * SGR colour (16 / 256 / truecolour), erase, scroll regions, the alternate
 * screen, and insert/delete. Links only the dependency-free parser + screen —
 * no display server required, so it runs in CI under ASan/UBSan.
 */
#include "neowall/terminal/terminal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fails = 0;
static int g_checks = 0;

static void feed(term_screen *s, const char *bytes) {
    term_screen_feed(s, (const uint8_t *)bytes, strlen(bytes));
}

/* Compare a visible row (ASCII glyphs; wide-tail cells skipped) to `want`. */
static void expect_line(term_screen *s, int y, const char *want) {
    g_checks++;
    const term_cell *r = term_screen_row(s, y);
    char buf[512];
    int o = 0, last = -1, cols = term_screen_cols(s);
    for (int x = 0; x < cols; x++) if (r[x].cp) last = x;
    for (int x = 0; x <= last && o < (int)sizeof(buf) - 1; x++) {
        uint32_t cp = r[x].cp;
        buf[o++] = cp ? (cp < 128 ? (char)cp : '?') : ' ';
    }
    buf[o] = 0;
    if (strcmp(buf, want) != 0) {
        printf("  FAIL row %d: got [%s] want [%s]\n", y, buf, want);
        g_fails++;
    }
}

static void expect(bool cond, const char *what) {
    g_checks++;
    if (!cond) { printf("  FAIL: %s\n", what); g_fails++; }
}

int main(void) {
    /* --- print + cursor positioning (CUP) --- */
    term_screen *s = term_screen_create(20, 6);
    feed(s, "Hello");
    expect_line(s, 0, "Hello");
    feed(s, "\x1b[2;3HXY");            /* row2 col3 */
    expect_line(s, 1, "  XY");
    term_screen_destroy(s);

    /* --- SGR: 256-colour + truecolour parse into structured colour --- */
    s = term_screen_create(20, 3);
    feed(s, "\x1b[38;5;196mRED\x1b[0m\x1b[38;2;10;20;30mTC");
    expect_line(s, 0, "REDTC");
    {
        const term_cell *r = term_screen_row(s, 0);
        expect(r[0].fg.kind == TERM_COLOR_INDEXED && r[0].fg.idx == 196, "256-colour idx 196");
        expect(r[3].fg.kind == TERM_COLOR_RGB && r[3].fg.r == 10 && r[3].fg.g == 20 && r[3].fg.b == 30,
               "truecolour 10,20,30");
    }
    term_screen_destroy(s);

    /* --- erase line to end (EL 0) --- */
    s = term_screen_create(10, 3);
    feed(s, "ABCDEFGHIJ\x1b[1;4H\x1b[0K");   /* cursor col4, clear to EOL -> ABC */
    expect_line(s, 0, "ABC");
    term_screen_destroy(s);

    /* --- scroll region (DECSTBM) + scroll-on-bottom --- */
    s = term_screen_create(6, 4);
    feed(s, "\x1b[2;3r");                  /* region rows 2..3 */
    feed(s, "\x1b[2;1HL2\r\nL3\r\nL4");     /* overfill -> scroll up within region */
    expect_line(s, 1, "L3");
    expect_line(s, 2, "L4");
    term_screen_destroy(s);

    /* --- alternate screen preserves primary --- */
    s = term_screen_create(8, 3);
    feed(s, "MAIN\x1b[?1049h\x1b[HALT");
    expect_line(s, 0, "ALT");
    feed(s, "\x1b[?1049l");
    expect_line(s, 0, "MAIN");
    term_screen_destroy(s);

    /* --- insert / delete chars (ICH / DCH) --- */
    s = term_screen_create(12, 2);
    feed(s, "ABCDEF\x1b[1;2H\x1b[2P");       /* delete 2 at col2 -> ADEF */
    expect_line(s, 0, "ADEF");
    feed(s, "\x1b[1;1H\x1b[3@");             /* insert 3 blanks at col1 */
    expect_line(s, 0, "   ADEF");
    term_screen_destroy(s);

    /* --- multibyte glyph (box-drawing) stored as one codepoint --- */
    s = term_screen_create(4, 1);
    feed(s, "\xE2\x94\x80");                /* U+2500 ─ */
    {
        const term_cell *r = term_screen_row(s, 0);
        expect(r[0].cp == 0x2500, "box-drawing U+2500 decoded");
    }
    term_screen_destroy(s);

    /* --- DEC special-graphics charset (ESC(0 + SI/SO) --- */
    s = term_screen_create(12, 1);
    feed(s, "\x1b(0qqxl\x1b(BAB");   /* q q ->─ ─, x ->│, l ->┌, then ASCII AB */
    {
        const term_cell *r = term_screen_row(s, 0);
        expect(r[0].cp == 0x2500, "DEC 'q' -> U+2500 horizontal");
        expect(r[1].cp == 0x2500, "DEC 'q' -> U+2500 horizontal (2)");
        expect(r[2].cp == 0x2502, "DEC 'x' -> U+2502 vertical");
        expect(r[3].cp == 0x250C, "DEC 'l' -> U+250C corner");
        expect(r[4].cp == 'A', "ESC(B restores ASCII 'A'");
        expect(r[5].cp == 'B', "ESC(B restores ASCII 'B'");
    }
    term_screen_destroy(s);

    /* --- SI/SO shift between G0(ASCII) and G1(DEC graphics) --- */
    s = term_screen_create(8, 1);
    feed(s, "\x1b)0"        /* designate G1 = DEC graphics */
            "A"            /* GL=G0=ASCII -> 'A' */
            "\x0e" "q"     /* SO -> GL=G1, 'q' -> U+2500 */
            "\x0f" "B");   /* SI -> GL=G0, 'B' */
    {
        const term_cell *r = term_screen_row(s, 0);
        expect(r[0].cp == 'A', "SI/SO: G0 'A'");
        expect(r[1].cp == 0x2500, "SI/SO: SO switches to G1 line-drawing");
        expect(r[2].cp == 'B', "SI/SO: SI switches back to G0");
    }
    term_screen_destroy(s);

    /* --- cursor visibility (DECTCEM) --- */
    s = term_screen_create(4, 1);
    expect(term_screen_cursor_visible(s), "cursor visible by default");
    feed(s, "\x1b[?25l");
    expect(!term_screen_cursor_visible(s), "DECTCEM ?25l hides cursor");
    feed(s, "\x1b[?25h");
    expect(term_screen_cursor_visible(s), "DECTCEM ?25h shows cursor");
    term_screen_destroy(s);

    /* --- OSC 0/2 window title --- */
    s = term_screen_create(8, 1);
    feed(s, "\x1b]0;My Title\x07");   /* OSC 0 ; text BEL */
    expect(strcmp(term_screen_title(s), "My Title") == 0, "OSC 0 sets title");
    feed(s, "\x1b]2;Second\x1b\\");   /* OSC 2 ; text ST */
    expect(strcmp(term_screen_title(s), "Second") == 0, "OSC 2 sets title (ST-terminated)");
    term_screen_destroy(s);

    /* --- query responses (DSR / Device Attributes) — the startup handshake
     * that probing TUIs like btop and vim block on. Without a reply they paint
     * once and freeze, so this is a load-bearing regression guard. --- */
    s = term_screen_create(20, 6);
    {
        char rep[128];
        /* DSR 5 -> "terminal OK" (CSI 0 n) */
        feed(s, "\x1b[5n");
        size_t n = term_screen_take_reply(s, rep, sizeof(rep));
        expect(n == 4 && memcmp(rep, "\x1b[0n", 4) == 0, "DSR 5n -> CSI 0n");

        /* DSR 6 (CPR) at a known cursor position -> CSI row;col R (1-based). */
        feed(s, "\x1b[3;7H\x1b[6n");
        n = term_screen_take_reply(s, rep, sizeof(rep));
        rep[n] = 0;
        expect(strcmp(rep, "\x1b[3;7R") == 0, "DSR 6n -> CPR row;col");

        /* Primary DA (CSI c) -> a VT-class attribute report starting CSI ? */
        feed(s, "\x1b[c");
        n = term_screen_take_reply(s, rep, sizeof(rep));
        expect(n >= 3 && rep[0] == 0x1b && rep[1] == '[' && rep[2] == '?' &&
               rep[n-1] == 'c', "Primary DA -> CSI ? ... c");

        /* Secondary DA (CSI > c) -> CSI > ... c */
        feed(s, "\x1b[>c");
        n = term_screen_take_reply(s, rep, sizeof(rep));
        expect(n >= 3 && rep[0] == 0x1b && rep[1] == '[' && rep[2] == '>' &&
               rep[n-1] == 'c', "Secondary DA -> CSI > ... c");

        /* Buffer is empty once drained. */
        n = term_screen_take_reply(s, rep, sizeof(rep));
        expect(n == 0, "reply buffer empty after drain");
    }
    term_screen_destroy(s);

    printf("terminal_screen: %d checks, %d failures\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
