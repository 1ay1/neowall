/* See neowall/base.h. */
#include "neowall/base.h"

#include <stdio.h>

static inline bool size_mul(size_t a, size_t b, size_t *out) {
    return !__builtin_mul_overflow(a, b, out);
}

/* ── arena ───────────────────────────────────────────────────────────── */

struct nw_arena_block {
    nw_arena_block *next;
    size_t used, cap;
    max_align_t data[];
};

#define NW_ARENA_MIN_BLOCK (16u * 1024u)

void nw_arena_init(nw_arena *a) { *a = (nw_arena){NULL, NW_ARENA_MIN_BLOCK}; }

void nw_arena_free(nw_arena *a) {
    nw_arena_block *b = a->head;
    while (b) {
        nw_arena_block *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

void *nw_arena_alloc(nw_arena *a, size_t size) {
    const size_t align = _Alignof(max_align_t);
    size_t need;
    if (!nw_size_add(size ? size : 1, align - 1, &need)) return NULL;
    need &= ~(align - 1);

    nw_arena_block *b = a->head;
    if (!b || b->cap - b->used < need) {
        size_t cap = need > a->block_size ? need : a->block_size;
        size_t bytes;
        if (!nw_size_add(sizeof(*b), cap, &bytes)) return NULL;
        b = malloc(bytes);
        if (!b) return NULL;
        b->next = a->head;
        b->used = 0;
        b->cap = cap;
        a->head = b;
    }
    void *p = (char *)b->data + b->used;
    b->used += need;
    memset(p, 0, size);
    return p;
}

char *nw_arena_strndup(nw_arena *a, const char *s, size_t n) {
    size_t bytes;
    if (!nw_size_add(n, 1, &bytes)) return NULL;
    char *p = nw_arena_alloc(a, bytes);
    if (p) memcpy(p, s, n);
    return p;
}

char *nw_arena_strdup(nw_arena *a, const char *s) {
    return s ? nw_arena_strndup(a, s, strlen(s)) : NULL;
}

char *nw_arena_printf(nw_arena *a, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *p = NULL;
    if (n >= 0 && (p = nw_arena_alloc(a, (size_t)n + 1))) vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

/* ── strings ─────────────────────────────────────────────────────────── */

void nw_str_free(nw_str *s) {
    free(s->data);
    *s = (nw_str){0};
}

void nw_str_clear(nw_str *s) {
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

static bool str_reserve(nw_str *s, size_t extra) {
    if (s->oom) return false;
    size_t need;
    if (!nw_size_add(s->len, extra, &need) || !nw_size_add(need, 1, &need)) {
        s->oom = true;
        return false;
    }
    if (need <= s->cap) return true;
    size_t cap = s->cap ? s->cap : 64;
    while (cap < need) {
        if (!size_mul(cap, 2, &cap)) {
            cap = need;
            break;
        }
    }
    char *p = realloc(s->data, cap);
    if (!p) {
        s->oom = true;
        return false;
    }
    s->data = p;
    s->cap = cap;
    return true;
}

bool nw_str_append_n(nw_str *s, const char *text, size_t n) {
    if (!str_reserve(s, n)) return false;
    memcpy(s->data + s->len, text, n);
    s->len += n;
    s->data[s->len] = '\0';
    return true;
}

bool nw_str_append(nw_str *s, const char *text) { return nw_str_append_n(s, text, strlen(text)); }

bool nw_str_printf(nw_str *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    bool ok = n >= 0 && str_reserve(s, (size_t)n);
    if (ok) {
        vsnprintf(s->data + s->len, (size_t)n + 1, fmt, ap2);
        s->len += (size_t)n;
    } else if (n < 0) {
        s->oom = true;
    }
    va_end(ap2);
    return ok;
}

char *nw_str_take(nw_str *s) {
    char *p = s->data;
    *s = (nw_str){0};
    return p;
}

/* ── list ────────────────────────────────────────────────────────────── */

bool nw_list_grow_(void **data, size_t *cap, size_t need, size_t elem) {
    if (need <= *cap) return true;
    size_t n = *cap ? *cap : 8;
    while (n < need) {
        if (!size_mul(n, 2, &n)) return false;
    }
    size_t bytes;
    if (!size_mul(n, elem, &bytes)) return false;
    void *p = realloc(*data, bytes);
    if (!p) return false;
    *data = p;
    *cap = n;
    return true;
}
