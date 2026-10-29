/* See neowall/shader/shader_hint.h. */
#include "neowall/shader/shader_hint.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "neowall/shader/shader_source.h"

#define MAX_KNOWN 512
static char g_known[MAX_KNOWN][40];
static size_t g_nknown;
static bool g_loaded;

static bool ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

static void add_known(const char *s, size_t len) {
    if (len == 0 || len >= sizeof(g_known[0]) || g_nknown >= MAX_KNOWN) return;
    for (size_t i = 0; i < g_nknown; i++)
        if (strlen(g_known[i]) == len && !memcmp(g_known[i], s, len)) return;
    memcpy(g_known[g_nknown], s, len);
    g_known[g_nknown][len] = '\0';
    g_nknown++;
}

/* Every nwFoo and iFoo name in the GLSL put in front of a shader. */
static void load_known(void) {
    if (g_loaded) return;
    g_loaded = true;
    const char *chunks[32];
    size_t n = nw_shader_preamble_chunks(chunks, 32);
    for (size_t c = 0; c < n; c++) {
        const char *s = chunks[c];
        for (const char *p = s; *p; p++) {
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

/* Case-insensitive Levenshtein distance, capped. */
static int edit_distance(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (la > 39 || lb > 39) return 99;
    int d[40][40];
    for (size_t i = 0; i <= la; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= lb; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= la; i++)
        for (size_t j = 1; j <= lb; j++) {
            int cost = tolower((unsigned char)a[i - 1]) != tolower((unsigned char)b[j - 1]);
            int m = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < m) m = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < m) m = d[i - 1][j - 1] + cost;
            d[i][j] = m;
        }
    return d[la][lb];
}

const char *shader_hint_suggest(const char *name) {
    if (!name || !*name) return NULL;
    load_known();
    /* also try the nw prefix: "fbm" -> "nwFbm" */
    char prefixed[48];
    snprintf(prefixed, sizeof(prefixed), "nw%c%s", toupper((unsigned char)name[0]), name + 1);
    const char *best = NULL;
    int best_d = 99;
    for (size_t i = 0; i < g_nknown; i++) {
        if (!strcmp(g_known[i], name)) return NULL; /* it exists: not a typo */
        int d = edit_distance(name, g_known[i]);
        int dp = edit_distance(prefixed, g_known[i]);
        if (dp < d) d = dp;
        if (d < best_d) {
            best_d = d;
            best = g_known[i];
        }
    }
    /* about one edit per 4 chars, 1 to 3 */
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

/* The last code line before `line` (skipping blanks, comments and
 * directives), trimmed, into buf. Its number, or 0. */
static int code_line_before(const char *file, int line, char *buf, size_t cap) {
    FILE *f = fopen(file, "r");
    if (!f) return 0;
    char cur[512];
    int n = 0, found = 0;
    while (n + 1 < line && fgets(cur, sizeof(cur), f)) {
        n++;
        size_t s = 0, t = strcspn(cur, "\r\n");
        while (s < t && (cur[s] == ' ' || cur[s] == '\t')) s++;
        while (t > s && (cur[t - 1] == ' ' || cur[t - 1] == '\t')) t--;
        bool comment = t - s >= 2 && cur[s] == '/' && (cur[s + 1] == '/' || cur[s + 1] == '*');
        if (t > s && !comment && cur[s] != '#') {
            snprintf(buf, cap, "%.*s", (int)(t - s), cur + s);
            found = n;
        }
    }
    fclose(f);
    return found;
}

bool shader_hint(const char *message, const char *file, int line, char *out, size_t cap) {
    char name[64];
    if (is_undeclared(message) && quoted_name(message, name, sizeof(name))) {
        const char *s = shader_hint_suggest(name);
        if (!s) return false;
        snprintf(out, cap, "did you mean %s?", s);
        return true;
    }
    /* a missing ; is only noticed at the next token, so the mistake is
     * usually at the end of the code line before */
    if (file && line > 1 &&
        (strstr(message, "expecting ',' or ';'") || strstr(message, "expected ';'") || strstr(message, "unexpected"))) {
        char prev[512];
        int at = code_line_before(file, line, prev, sizeof(prev));
        size_t len = strlen(prev);
        if (at && len && !strchr(";{},", prev[len - 1])) {
            snprintf(out, cap, "line %d may be missing a ';' at the end", at);
            return true;
        }
    }
    return false;
}
