/* See include/neowall/shader/glsl_shadow.h for the rationale. */

#include "neowall/shader/glsl_shadow.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Shaders define a handful of functions, not hundreds; start small and grow. */
#define SHADOW_INITIAL_CAP 16

struct glsl_shadow_set {
    char **names;
    size_t count;
    size_t cap;
};

glsl_shadow_set *glsl_shadow_create(void) {
    glsl_shadow_set *set = calloc(1, sizeof(*set));
    return set; /* names allocated lazily on first insert */
}

void glsl_shadow_free(glsl_shadow_set *set) {
    if (!set) return;
    for (size_t i = 0; i < set->count; i++) free(set->names[i]);
    free(set->names);
    free(set);
}

size_t glsl_shadow_count(const glsl_shadow_set *set) {
    return set ? set->count : 0;
}

const char *glsl_shadow_at(const glsl_shadow_set *set, size_t index) {
    if (!set || index >= set->count) return NULL;
    return set->names[index];
}

bool glsl_shadow_contains(const glsl_shadow_set *set, const char *name) {
    if (!set || !name) return false;
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->names[i], name) == 0) return true;
    }
    return false;
}

/* Insert `name` (length `len`) if not already present. False on OOM only. */
static bool shadow_add(glsl_shadow_set *set, const char *name, size_t len) {
    if (len == 0) return true;

    for (size_t i = 0; i < set->count; i++) {
        if (strncmp(set->names[i], name, len) == 0 && set->names[i][len] == '\0') {
            return true; /* already known */
        }
    }

    if (set->count == set->cap) {
        size_t ncap = set->cap ? set->cap * 2 : SHADOW_INITIAL_CAP;
        char **grown = realloc(set->names, ncap * sizeof(*grown));
        if (!grown) return false;
        set->names = grown;
        set->cap = ncap;
    }

    char *copy = malloc(len + 1);
    if (!copy) return false;
    memcpy(copy, name, len);
    copy[len] = '\0';
    set->names[set->count++] = copy;
    return true;
}

static bool is_ident_start(int c) { return isalpha(c) || c == '_'; }
static bool is_ident_char(int c) { return isalnum(c) || c == '_'; }

/* Walk back from `end` (exclusive) over whitespace, returning the new index. */
static size_t skip_space_back(const char *s, size_t end) {
    while (end > 0 && isspace((unsigned char)s[end - 1])) end--;
    return end;
}

/* GLSL reserved words that can precede a name but are never the name itself.
 * If the token immediately before `(` is one of these, we are looking at a
 * control-flow construct (`if (...)`, `while (...)`), not a definition. */
static bool is_control_keyword(const char *s, size_t len) {
    static const char *const kw[] = {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "return", "discard", "break", "continue",
    };
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        if (strncmp(s, kw[i], len) == 0 && kw[i][len] == '\0') return true;
    }
    return false;
}

/* Scan a `#define` directive at `i` (pointing at the '#'), recording the macro
 * name. Returns the index just past the directive, handling line continuations
 * so a multi-line macro body is skipped wholesale. */
static size_t scan_define(glsl_shadow_set *set, const char *s, size_t n, size_t i,
                          bool *oom) {
    size_t j = i + 1;
    while (j < n && isspace((unsigned char)s[j]) && s[j] != '\n') j++;

    const char *kw = "define";
    size_t kwlen = 6;
    bool is_define = (j + kwlen <= n) && strncmp(s + j, kw, kwlen) == 0 &&
                     (j + kwlen == n || !is_ident_char((unsigned char)s[j + kwlen]));

    if (is_define) {
        j += kwlen;
        while (j < n && isspace((unsigned char)s[j]) && s[j] != '\n') j++;
        size_t start = j;
        while (j < n && is_ident_char((unsigned char)s[j])) j++;
        if (j > start && !shadow_add(set, s + start, j - start)) {
            *oom = true;
        }
    }

    /* Skip to the end of the directive, respecting backslash continuations. */
    while (j < n) {
        if (s[j] == '\\' && j + 1 < n) {
            size_t k = j + 1;
            while (k < n && (s[k] == ' ' || s[k] == '\t' || s[k] == '\r')) k++;
            if (k < n && s[k] == '\n') { j = k + 1; continue; }
        }
        if (s[j] == '\n') { j++; break; }
        j++;
    }
    return j;
}

bool glsl_shadow_scan(glsl_shadow_set *set, const char *source) {
    if (!set || !source) return true;

    const char *s = source;
    size_t n = strlen(s);
    int depth = 0;       /* brace nesting; definitions live at depth 0 */
    int paren = 0;       /* paren nesting, to ignore commas inside arg lists */
    bool oom = false;

    for (size_t i = 0; i < n;) {
        char c = s[i];

        /* ---- comments: skipped so prose never votes on the namespace ---- */
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            i += 2;
            while (i < n && s[i] != '\n') i++;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }

        /* ---- preprocessor: only `#define` introduces a name ---- */
        if (c == '#') {
            /* Only treat as a directive if '#' is first non-space on its line. */
            size_t k = i;
            bool at_line_start = true;
            while (k > 0) {
                char p = s[k - 1];
                if (p == '\n') break;
                if (!isspace((unsigned char)p)) { at_line_start = false; break; }
                k--;
            }
            if (at_line_start) {
                i = scan_define(set, s, n, i, &oom);
                if (oom) return false;
                continue;
            }
        }

        if (c == '{') { depth++; i++; continue; }
        if (c == '}') { if (depth > 0) depth--; i++; continue; }
        if (c == '(') { paren++; i++; continue; }
        if (c == ')') { if (paren > 0) paren--; i++; continue; }

        /* ---- function definitions / prototypes at top level ----
         * Pattern: <type> <name> (   with `name` immediately before the paren.
         * We detect the '(' and look backwards for the identifier and the type
         * token that must precede it. Requiring a preceding token is what
         * separates `float sdBox(` (a definition) from `sdBox(` (a call). */
        if (is_ident_start((unsigned char)c) && depth == 0 && paren == 0) {
            size_t start = i;
            while (i < n && is_ident_char((unsigned char)s[i])) i++;
            size_t len = i - start;

            size_t after = i;
            while (after < n && isspace((unsigned char)s[after])) after++;
            if (after < n && s[after] == '(' && !is_control_keyword(s + start, len)) {
                /* Require a type-ish token before the name. */
                size_t before = skip_space_back(s, start);
                if (before > 0 && is_ident_char((unsigned char)s[before - 1])) {
                    size_t tstart = before;
                    while (tstart > 0 && is_ident_char((unsigned char)s[tstart - 1])) tstart--;
                    /* `return foo(` is a call, not a definition. */
                    if (!is_control_keyword(s + tstart, before - tstart)) {
                        if (!shadow_add(set, s + start, len)) return false;
                    }
                }
            }
            continue;
        }

        i++;
    }

    return !oom;
}
