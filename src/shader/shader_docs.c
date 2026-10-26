/* `neowall docs`. See include/neowall/shader/shader_docs.h. */

#define _GNU_SOURCE   /* strcasestr */
#include "neowall/shader/shader_docs.h"

#include <ctype.h>
#include <string.h>

static void squeeze(char *s) {
    /* Collapse runs of whitespace into one space; trim. */
    char *w = s;
    bool sp = true;
    for (char *r = s; *r; r++) {
        if (isspace((unsigned char)*r)) {
            if (!sp) *w++ = ' ';
            sp = true;
        } else {
            *w++ = *r;
            sp = false;
        }
    }
    if (w > s && w[-1] == ' ') w--;
    *w = '\0';
}

static void append(char *dst, size_t size, const char *src, size_t len) {
    size_t n = strlen(dst);
    if (n && n + 1 < size) dst[n++] = ' ';
    if (len > size - n - 1) len = size - n - 1;
    memcpy(dst + n, src, len);
    dst[n + len] = '\0';
}

static bool seen(const shader_doc_entry *out, size_t n, const char *sig) {
    for (size_t i = 0; i < n; i++)
        if (!strcmp(out[i].signature, sig)) return true;
    return false;
}

/* One logical line of GLSL. Returns false at the end. */
static bool next_line(const char **p, const char **s, size_t *len) {
    if (!**p) return false;
    *s = *p;
    const char *e = strchr(*p, '\n');
    *len = e ? (size_t)(e - *p) : strlen(*p);
    *p = e ? e + 1 : *p + *len;
    return true;
}

size_t shader_docs_collect(const char *const *chunks, size_t nchunks,
                           shader_doc_entry *out, size_t max) {
    size_t n = 0;
    for (size_t c = 0; c < nchunks; c++) {
        const char *p = chunks[c];
        if (!p) continue;
        char doc[240] = "";
        char section[64] = "";
        const char *s;
        size_t len;
        while (n < max && next_line(&p, &s, &len)) {
            while (len && isspace((unsigned char)*s)) { s++; len--; }
            if (len >= 2 && s[0] == '/' && s[1] == '/') {
                /* Comment: collect. A ruler like "// ---- value noise ----"
                 * names the section the following entries belong to. */
                const char *t = s + 2;
                size_t tl = len - 2;
                while (tl && (*t == ' ' || *t == '-' || *t == '=')) { t++; tl--; }
                while (tl && (t[tl - 1] == ' ' || t[tl - 1] == '-' || t[tl - 1] == '=')) tl--;
                bool ruler = len >= 6 && (s[3] == '-' || s[3] == '=');
                if (ruler) {
                    doc[0] = '\0';
                    if (tl) snprintf(section, sizeof(section), "%.*s", (int)tl, t);
                } else if (tl) {
                    append(doc, sizeof(doc), t, tl);
                }
                continue;
            }

            shader_doc_entry e = {0};
            bool ok = false;
            if (len > 8 && !strncmp(s, "uniform ", 8)) {
                /* uniform <type> iName[...]; */
                const char *nm = s + 8;
                while (nm < s + len && *nm != ' ') nm++;
                while (nm < s + len && *nm == ' ') nm++;
                if (nm < s + len && nm[0] == 'i' && isupper((unsigned char)nm[1])) {
                    const char *semi = memchr(s, ';', len);
                    size_t sl = semi ? (size_t)(semi - s) : len;
                    if (sl >= sizeof(e.signature)) sl = sizeof(e.signature) - 1;
                    memcpy(e.signature, s, sl);
                    const char *ne = nm;
                    while (isalnum((unsigned char)*ne) || *ne == '_') ne++;
                    snprintf(e.name, sizeof(e.name), "%.*s", (int)(ne - nm), nm);
                    e.uniform = true;
                    /* Trailing // comment on the same line. */
                    const char *cm = semi ? strstr(semi, "//") : NULL;
                    if (cm && cm < s + len) {
                        cm += 2;
                        while (*cm == ' ') cm++;
                        doc[0] = '\0';
                        append(doc, sizeof(doc), cm, (size_t)(s + len - cm));
                    }
                    ok = true;
                }
            } else {
                /* <type> nwName(<args>) { ... */
                const char *nm = strstr(s, " nw");
                const char *par = nm ? memchr(nm, '(', (size_t)(s + len - nm)) : NULL;
                const char *close = par ? memchr(par, ')', (size_t)(s + len - par)) : NULL;
                if (nm && par && close && nm < s + len && isupper((unsigned char)nm[3]) &&
                    strncmp(s, "return ", 7) != 0 &&
                    memchr(s, '=', (size_t)(nm - s)) == NULL) {
                    bool decl = true;
                    for (const char *q = nm + 1; q < par; q++)
                        if (!isalnum((unsigned char)*q) && *q != '_') decl = false;
                    if (decl) {
                        size_t sl = (size_t)(close + 1 - s);
                        if (sl >= sizeof(e.signature)) sl = sizeof(e.signature) - 1;
                        memcpy(e.signature, s, sl);
                        snprintf(e.name, sizeof(e.name), "%.*s", (int)(par - nm - 1), nm + 1);
                        ok = true;
                    }
                }
            }
            if (ok) {
                squeeze(e.signature);
                snprintf(e.doc, sizeof(e.doc), "%s", doc);
                squeeze(e.doc);
                snprintf(e.section, sizeof(e.section), "%s", section);
                if (!seen(out, n, e.signature)) out[n++] = e;
            }
            if (len) doc[0] = '\0';   /* a code line ends the comment block */
        }
    }
    return n;
}

static bool matches(const shader_doc_entry *e, const char *q) {
    if (!q || !*q) return true;
    return strcasestr(e->name, q) || strcasestr(e->doc, q) || strcasestr(e->signature, q) ||
           strcasestr(e->section, q);
}

size_t shader_docs_print(FILE *f, const shader_doc_entry *e, size_t n,
                         const char *query, bool color) {
    const char *bold = color ? "\033[1m" : "", *dim = color ? "\033[2m" : "";
    const char *head = color ? "\033[1;36m" : "", *off = color ? "\033[0m" : "";
    size_t shown = 0;
    for (int pass = 0; pass < 2; pass++) {
        bool want_uniform = pass == 0;
        bool header = false;
        for (size_t i = 0; i < n; i++) {
            if (e[i].uniform != want_uniform || !matches(&e[i], query)) continue;
            if (!header) {
                fprintf(f, "%s%s%s\n", head, want_uniform ? "Uniforms" : "Functions", off);
                header = true;
            }
            fprintf(f, "  %s%s%s\n", bold, e[i].signature, off);
            if (e[i].doc[0]) fprintf(f, "      %s%s%s\n", dim, e[i].doc, off);
            shown++;
        }
        if (header) fputc('\n', f);
    }
    return shown;
}
