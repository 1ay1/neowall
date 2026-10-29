/* neowall/base.h — small helpers the shader code builds on.
 *
 *   nw_str      a length-tracked, growable string. Never overflows.
 *   nw_arena    a bump allocator; everything in it is freed at once.
 *   NW_LIST(T)  a typed growable array (nw_list_push / nw_list_free).
 *
 * Ported from the rewrite's core; NW_LIST is what it calls NW_VEC, renamed
 * so it doesn't clash with neowall/vec.h. */
#ifndef NEOWALL_BASE_H
#define NEOWALL_BASE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/result.h"

#define NW_MUST_USE __attribute__((warn_unused_result))

#define NW_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#define NW_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

/* ── arena ───────────────────────────────────────────────────────────── */

typedef struct nw_arena_block nw_arena_block;
typedef struct {
    nw_arena_block *head;
    size_t block_size;
} nw_arena;

void nw_arena_init(nw_arena *a);
void nw_arena_free(nw_arena *a);
/* Zeroed, max-aligned. NULL only on OOM or size overflow. */
void *nw_arena_alloc(nw_arena *a, size_t size);
char *nw_arena_strdup(nw_arena *a, const char *s);
char *nw_arena_strndup(nw_arena *a, const char *s, size_t n);
char *nw_arena_printf(nw_arena *a, const char *fmt, ...) NW_PRINTF(2, 3);

/* ── strings ─────────────────────────────────────────────────────────── */

typedef struct nw_str {
    char *data; /* always NUL-terminated when non-NULL */
    size_t len, cap;
    bool oom;   /* sticky: once an append fails, the string is unusable */
} nw_str;

void nw_str_free(nw_str *s);
void nw_str_clear(nw_str *s);
bool nw_str_append(nw_str *s, const char *text);
bool nw_str_append_n(nw_str *s, const char *text, size_t n);
bool nw_str_printf(nw_str *s, const char *fmt, ...) NW_PRINTF(2, 3);
/* The text, or "" for an empty string. */
static inline const char *nw_str_cstr(const nw_str *s) { return s->data ? s->data : ""; }
/* Hand the buffer to the caller (free() it). The string is left empty. */
char *nw_str_take(nw_str *s);

static inline bool nw_size_add(size_t a, size_t b, size_t *out) {
    return !__builtin_add_overflow(a, b, out);
}

/* ── list ────────────────────────────────────────────────────────────── */

/* NW_LIST(int) v = {0}; nw_list_push(&v, 3); for (i < v.len) v.data[i]; */
#define NW_LIST(T) struct { T *data; size_t len, cap; }

/* Named list types for things passed between functions (two anonymous
 * NW_LIST(T) declarations are distinct types). */
typedef NW_LIST(const char *) nw_strs;

bool nw_list_grow_(void **data, size_t *cap, size_t need, size_t elem);

#define nw_list_reserve(v, n) \
    nw_list_grow_((void **)&(v)->data, &(v)->cap, (n), sizeof(*(v)->data))
#define nw_list_push(v, x) \
    (nw_list_reserve((v), (v)->len + 1) ? ((v)->data[(v)->len++] = (x), true) : false)
#define nw_list_free(v) (free((v)->data), (v)->data = NULL, (v)->len = (v)->cap = 0)

#endif /* NEOWALL_BASE_H */
