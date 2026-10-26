/* Shader diagnostics. See include/neowall/shader/shader_diag.h. */

#include "neowall/shader/shader_diag.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── parsing ────────────────────────────────────────────────────────── */

/* Read a decimal at *p; advance past it. -1 if none. */
static int read_int(const char **p) {
    if (!isdigit((unsigned char)**p)) return -1;
    int v = 0;
    while (isdigit((unsigned char)**p)) v = v * 10 + (*(*p)++ - '0');
    return v;
}

/* Match "<file>(<line>)" or "<file>:<line>(<col>)" or "<file>:<line>:" at p.
 * Sets line/col and returns a pointer past it, or NULL. */
static const char *match_location(const char *p, int *line, int *col) {
    const char *q = p;
    if (read_int(&q) < 0) return NULL;          /* source string index */
    *col = 0;
    if (*q == '(') {                            /* NVIDIA: 0(17) */
        q++;
        *line = read_int(&q);
        if (*line < 0 || *q != ')') return NULL;
        return q + 1;
    }
    if (*q == ':') {                            /* Mesa: 0:17(5)  others: 0:17: */
        q++;
        *line = read_int(&q);
        if (*line < 0) return NULL;
        if (*q == '(') {
            q++;
            *col = read_int(&q);
            if (*col < 0 || *q != ')') return NULL;
            q++;
        }
        return q;
    }
    return NULL;
}

/* Copy msg into out, trimmed, dropping driver codes like "C1503:" and the
 * word "error:"/"warning:" itself. */
static void clean_message(const char *msg, size_t len, char *out, size_t size) {
    while (len && isspace((unsigned char)*msg)) { msg++; len--; }
    while (len && (isspace((unsigned char)msg[len - 1]) || msg[len - 1] == '\r')) len--;
    /* "C1503: " / "C0000: " (NVIDIA codes) */
    if (len > 6 && msg[0] == 'C' && isdigit((unsigned char)msg[1]) &&
        isdigit((unsigned char)msg[4]) && msg[5] == ':') {
        msg += 6; len -= 6;
        while (len && *msg == ' ') { msg++; len--; }
    }
    if (len >= size) len = size - 1;
    memcpy(out, msg, len);
    out[len] = '\0';
}

size_t shader_diag_parse(const char *log, shader_diag *out, size_t max) {
    size_t n = 0;
    if (!log) return 0;
    for (const char *line = log; *line && n < max;) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        const char *p = line, *end = line + len;
        bool warning = false;

        /* Optional "ERROR: " / "WARNING: " in front (ANGLE, some mobile). */
        if (!strncmp(p, "ERROR: ", 7)) p += 7;
        else if (!strncmp(p, "WARNING: ", 9)) { p += 9; warning = true; }

        int ln = 0, col = 0;
        const char *after = match_location(p, &ln, &col);
        if (after && after < end) {
            while (after < end && (*after == ' ' || *after == ':')) after++;
            if (!strncmp(after, "error", 5)) {
                after += 5;
            } else if (!strncmp(after, "warning", 7)) {
                after += 7;
                warning = true;
            }
            while (after < end && (*after == ' ' || *after == ':')) after++;

            shader_diag d = {.line = ln, .column = col, .warning = warning};
            clean_message(after, (size_t)(end - after), d.message, sizeof(d.message));

            bool dup = false;
            for (size_t i = 0; i < n; i++)
                if (out[i].line == d.line && !strcmp(out[i].message, d.message)) dup = true;
            if (!dup && d.message[0]) out[n++] = d;
        }
        if (!eol) break;
        line = eol + 1;
    }
    return n;
}

/* ── suggestions ────────────────────────────────────────────────────── */

#define MAX_KNOWN 512
static char g_known[MAX_KNOWN][40];
static size_t g_nknown;

static bool ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

static void add_known(const char *s, size_t len) {
    if (len == 0 || len >= sizeof(g_known[0]) || g_nknown >= MAX_KNOWN) return;
    for (size_t i = 0; i < g_nknown; i++)
        if (strlen(g_known[i]) == len && !memcmp(g_known[i], s, len)) return;
    memcpy(g_known[g_nknown], s, len);
    g_known[g_nknown][len] = '\0';
    g_nknown++;
}

void shader_diag_set_known(const char *const *chunks, size_t n) {
    g_nknown = 0;
    for (size_t c = 0; c < n; c++) {
        const char *s = chunks[c];
        if (!s) continue;
        for (const char *p = s; *p; p++) {
            /* Identifier start, not inside another identifier. */
            if ((p > s && ident_char(p[-1])) || !(*p == 'n' || *p == 'i')) continue;
            bool nw = p[0] == 'n' && p[1] == 'w' && isupper((unsigned char)p[2]);
            bool iu = p[0] == 'i' && isupper((unsigned char)p[1]);
            if (!nw && !iu) continue;
            const char *e = p;
            while (ident_char(*e)) e++;
            add_known(p, (size_t)(e - p));
            p = e - 1;
        }
    }
}

static int lower(int c) { return tolower((unsigned char)c); }

/* Case-insensitive Levenshtein distance, capped. */
static int edit_distance(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > 39 || lb > 39) return 99;
    int d[40][40];
    for (size_t i = 0; i <= la; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= lb; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= la; i++)
        for (size_t j = 1; j <= lb; j++) {
            int cost = lower(a[i - 1]) != lower(b[j - 1]);
            int m = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < m) m = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < m) m = d[i - 1][j - 1] + cost;
            d[i][j] = m;
        }
    return d[la][lb];
}

const char *shader_diag_suggest(const char *name) {
    if (!name || !*name) return NULL;
    /* Also try with the nw prefix: "fbm" -> "nwFbm", "sdBox" -> "nwSdBox". */
    char prefixed[48];
    snprintf(prefixed, sizeof(prefixed), "nw%c%s", toupper((unsigned char)name[0]), name + 1);

    const char *best = NULL;
    int best_d = 99;
    for (size_t i = 0; i < g_nknown; i++) {
        if (!strcmp(g_known[i], name)) return NULL;   /* it exists; not a typo */
        int d = edit_distance(name, g_known[i]);
        int dp = edit_distance(prefixed, g_known[i]);
        if (dp < d) d = dp;
        if (d < best_d) { best_d = d; best = g_known[i]; }
    }
    /* Allow ~1 edit per 4 chars, at least 1, at most 3. */
    int limit = (int)strlen(name) / 4;
    if (limit < 1) limit = 1;
    if (limit > 3) limit = 3;
    return best_d <= limit ? best : NULL;
}

/* The identifier a message complains about: `foo' / 'foo' / "foo". */
static bool quoted_name(const char *msg, char *out, size_t size) {
    for (const char *p = msg; *p; p++) {
        if (*p != '\'' && *p != '"' && *p != '`') continue;
        const char *s = p + 1, *e = s;
        while (ident_char(*e)) e++;
        if (e > s && (*e == '\'' || *e == '"') && (size_t)(e - s) < size) {
            memcpy(out, s, (size_t)(e - s));
            out[e - s] = '\0';
            return true;
        }
    }
    return false;
}

static bool is_undeclared(const char *msg) {
    return strstr(msg, "undeclared") || strstr(msg, "undefined variable") ||
           strstr(msg, "no matching overloaded function") || strstr(msg, "undefined function") ||
           strstr(msg, "no function with name");
}

/* ── formatting ─────────────────────────────────────────────────────── */

/* Pointer to the start of 1-based line `ln` in src, and its length. */
static const char *source_line(const char *src, int ln, size_t *len) {
    if (!src || ln < 1) return NULL;
    const char *p = src;
    for (int i = 1; i < ln; i++) {
        p = strchr(p, '\n');
        if (!p) return NULL;
        p++;
    }
    const char *e = strchr(p, '\n');
    *len = e ? (size_t)(e - p) : strlen(p);
    return p;
}

#define APPEND(...) do { \
    if (n < size) { int w_ = snprintf(buf + n, size - n, __VA_ARGS__); \
                    if (w_ > 0) n += (size_t)w_ < size - n ? (size_t)w_ : size - n - 1; } \
} while (0)

size_t shader_diag_format(const char *log, const char *source, const char *path,
                          bool color, char *buf, size_t size) {
    if (!buf || size == 0) return 0;
    buf[0] = '\0';
    shader_diag d[32];
    size_t nd = shader_diag_parse(log, d, 32);
    if (nd == 0) return 0;

    const char *red = color ? "\033[1;31m" : "", *yel = color ? "\033[1;33m" : "";
    const char *dim = color ? "\033[2m" : "", *bold = color ? "\033[1m" : "";
    const char *cyan = color ? "\033[36m" : "", *off = color ? "\033[0m" : "";
    const char *base = path ? strrchr(path, '/') : NULL;
    base = base ? base + 1 : (path ? path : "shader");

    size_t n = 0;
    int errors = 0;
    for (size_t i = 0; i < nd; i++) {
        if (!d[i].warning) errors++;
        if (d[i].line > 0) APPEND("%s%s:%d%s: ", bold, base, d[i].line, off);
        else APPEND("%s%s%s: ", bold, base, off);
        APPEND("%s%s%s %s\n", d[i].warning ? yel : red,
               d[i].warning ? "warning:" : "error:", off, d[i].message);

        size_t len = 0;
        const char *src = source_line(source, d[i].line, &len);
        if (src) {
            /* Show the line with tabs as spaces, so the caret lines up. */
            char shown[256];
            size_t k = 0, first = 0;
            bool seen = false;
            for (size_t j = 0; j < len && k < sizeof(shown) - 1; j++) {
                char c = src[j] == '\t' ? ' ' : src[j];
                if (!seen && c != ' ') { first = k; seen = true; }
                shown[k++] = c;
            }
            shown[k] = '\0';
            APPEND("  %s%5d |%s %s\n", dim, d[i].line, off, shown);

            /* Caret under the column, or under the complained-about name, or
             * under the start of the code on that line. */
            size_t at = first;
            char name[64];
            if (d[i].column > 0 && (size_t)d[i].column - 1 < k) {
                at = (size_t)d[i].column - 1;
            } else if (quoted_name(d[i].message, name, sizeof(name))) {
                const char *hit = strstr(shown, name);
                if (hit) at = (size_t)(hit - shown);
            }
            APPEND("  %s      |%s %*s%s^%s\n", dim, off, (int)at, "", d[i].warning ? yel : red, off);
        }

        char name[64];
        if (is_undeclared(d[i].message) && quoted_name(d[i].message, name, sizeof(name))) {
            const char *s = shader_diag_suggest(name);
            if (s) APPEND("  %shelp:%s did you mean %s%s%s?\n", cyan, off, bold, s, off);
        } else if (strstr(d[i].message, "expecting ',' or ';'") ||
                   strstr(d[i].message, "expected ';'") ||
                   strstr(d[i].message, "unexpected") ) {
            /* The compiler notices a missing ; only at the next token, so the
             * real mistake is usually at the end of the previous code line. */
            int prev = d[i].line - 1;
            size_t plen = 0;
            const char *p = NULL;
            while (prev >= 1) {
                p = source_line(source, prev, &plen);
                if (!p) break;
                size_t s = 0, t = plen;
                while (s < t && (p[s] == ' ' || p[s] == '\t')) s++;
                while (t > s && (p[t - 1] == ' ' || p[t - 1] == '\t' || p[t - 1] == '\r')) t--;
                bool comment = t - s >= 2 && p[s] == '/' && (p[s + 1] == '/' || p[s + 1] == '*');
                if (t > s && !comment && p[s] != '#') { plen = t; break; }
                prev--;
            }
            if (p && prev >= 1 && plen && p[plen - 1] != ';' && p[plen - 1] != '{' &&
                p[plen - 1] != '}' && p[plen - 1] != ',') {
                APPEND("  %shelp:%s line %d may be missing a ';' at the end\n", cyan, off, prev);
            }
        }
    }
    if (errors > 1) APPEND("%d errors\n", errors);
    return n;
}
