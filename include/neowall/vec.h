/*
 * neowall/vec.h — type-safe growable arrays for C
 * ============================================================================
 *
 * A single, header-only, type-generic dynamic array. Replaces the fixed-size
 * `T arr[MAX_N]` declarations scattered through the codebase (MAX_OUTPUTS,
 * MAX_WALLPAPERS, ...) which silently truncate and impose arbitrary caps.
 *
 * Two ways to use it:
 *
 *  1. Ad-hoc, via the void-based core (no codegen):
 *
 *       nw_vec v;
 *       nw_vec_init(&v, sizeof(int));
 *       int x = 42;
 *       nw_vec_push(&v, &x);
 *       int *p = nw_vec_at(&v, 0);
 *       nw_vec_free(&v, NULL);
 *
 *  2. Typed, via NW_VEC_DECLARE / NW_VEC_DEFINE (recommended for public APIs):
 *
 *       NW_VEC_DECLARE(int_vec, int)        // in a header
 *       NW_VEC_DEFINE (int_vec, int)        // in one .c file
 *
 *       int_vec v; int_vec_init(&v);
 *       int_vec_push(&v, 42);
 *       int x = *int_vec_at(&v, 0);
 *       for (size_t i = 0; i < v.len; i++) { ... v.data[i] ... }
 *       int_vec_free(&v);
 *
 * Memory model:
 *   - Grows geometrically (1.5x, min 8) so N pushes are amortised O(N).
 *   - `data` is contiguous; `&v.data[i]` is stable until the next growth.
 *   - nw_vec_free takes an optional element destructor for vectors of owning
 *     pointers; pass NULL when elements are POD.
 *   - On OOM, push returns false and the vector is left unchanged (strong
 *     guarantee). Callers that ignore the return get the old contents intact.
 */
#ifndef NEOWALL_VEC_H
#define NEOWALL_VEC_H

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Void-based core (used by the typed wrappers; usable directly too)
 * ============================================================================ */

typedef struct nw_vec {
    void  *data;        /* contiguous storage, or NULL when cap == 0 */
    size_t len;         /* number of live elements */
    size_t cap;         /* allocated capacity in elements */
    size_t elem_size;   /* sizeof(element) */
} nw_vec;

static inline void nw_vec_init(nw_vec *v, size_t elem_size) {
    v->data = NULL;
    v->len = 0;
    v->cap = 0;
    v->elem_size = elem_size;
}

/* Ensure capacity for at least `want` elements. Returns false on OOM. */
static inline bool nw_vec_reserve(nw_vec *v, size_t want) {
    if (want <= v->cap) {
        return true;
    }
    size_t new_cap = v->cap ? v->cap : 8;
    while (new_cap < want) {
        size_t grown = new_cap + new_cap / 2;   /* 1.5x */
        new_cap = grown > new_cap ? grown : new_cap + 1;  /* guard overflow */
    }
    void *p = realloc(v->data, new_cap * v->elem_size);
    if (!p) {
        return false;
    }
    v->data = p;
    v->cap = new_cap;
    return true;
}

/* Append a copy of *elem. Returns false on OOM (vector unchanged). */
static inline bool nw_vec_push(nw_vec *v, const void *elem) {
    if (!nw_vec_reserve(v, v->len + 1)) {
        return false;
    }
    memcpy((char *)v->data + v->len * v->elem_size, elem, v->elem_size);
    v->len++;
    return true;
}

/* Pointer to element i, or NULL if out of range. Stable until next growth. */
static inline void *nw_vec_at(const nw_vec *v, size_t i) {
    if (i >= v->len) {
        return NULL;
    }
    return (char *)v->data + i * v->elem_size;
}

/* Drop all elements (len -> 0) without releasing capacity. Runs `dtor` on each
 * element first if provided. */
static inline void nw_vec_clear(nw_vec *v, void (*dtor)(void *)) {
    if (dtor) {
        for (size_t i = 0; i < v->len; i++) {
            dtor((char *)v->data + i * v->elem_size);
        }
    }
    v->len = 0;
}

/* Release all storage. Runs `dtor` on each live element first if provided. */
static inline void nw_vec_free(nw_vec *v, void (*dtor)(void *)) {
    nw_vec_clear(v, dtor);
    free(v->data);
    v->data = NULL;
    v->cap = 0;
}

/* ============================================================================
 * Typed wrapper generator
 * ============================================================================
 *
 * NW_VEC_DECLARE(Name, T) emits the struct + prototypes (put in a header).
 * NW_VEC_DEFINE (Name, T) emits the function bodies   (put in exactly one .c).
 *
 * For a header-only typed vector, use NW_VEC_DECLARE followed immediately by
 * NW_VEC_DEFINE with `static inline` linkage via NW_VEC_DEFINE_STATIC.
 */

#define NW_VEC_DECLARE(Name, T)                                                \
    typedef struct Name {                                                      \
        T     *data;                                                           \
        size_t len;                                                            \
        size_t cap;                                                            \
    } Name;                                                                    \
    void  Name##_init(Name *v);                                                \
    void  Name##_free(Name *v);                                                \
    bool  Name##_push(Name *v, T value);                                       \
    T    *Name##_at(const Name *v, size_t i);                                  \
    bool  Name##_reserve(Name *v, size_t want);                                \
    void  Name##_clear(Name *v);

#define NW_VEC__BODY(Name, T, LINKAGE)                                         \
    LINKAGE void Name##_init(Name *v) {                                        \
        v->data = NULL; v->len = 0; v->cap = 0;                               \
    }                                                                          \
    LINKAGE bool Name##_reserve(Name *v, size_t want) {                       \
        nw_vec core = { v->data, v->len, v->cap, sizeof(T) };                 \
        bool ok = nw_vec_reserve(&core, want);                                \
        v->data = (T *)core.data; v->cap = core.cap;                          \
        return ok;                                                             \
    }                                                                          \
    LINKAGE bool Name##_push(Name *v, T value) {                              \
        if (!Name##_reserve(v, v->len + 1)) return false;                     \
        v->data[v->len++] = value;                                            \
        return true;                                                           \
    }                                                                          \
    LINKAGE T *Name##_at(const Name *v, size_t i) {                           \
        return i < v->len ? &v->data[i] : NULL;                               \
    }                                                                          \
    LINKAGE void Name##_clear(Name *v) { v->len = 0; }                        \
    LINKAGE void Name##_free(Name *v) {                                       \
        free(v->data); v->data = NULL; v->len = 0; v->cap = 0;                \
    }

#define NW_VEC_DEFINE(Name, T)         NW_VEC__BODY(Name, T, /* extern */)

/* Self-contained header-only typed vector: emits the struct AND static-inline
 * bodies in one shot. Do NOT precede this with NW_VEC_DECLARE (that would
 * redeclare the functions with external linkage). */
#define NW_VEC_DEFINE_STATIC(Name, T)                                         \
    typedef struct Name {                                                     \
        T     *data;                                                          \
        size_t len;                                                           \
        size_t cap;                                                           \
    } Name;                                                                   \
    NW_VEC__BODY(Name, T, static inline)

#endif /* NEOWALL_VEC_H */
