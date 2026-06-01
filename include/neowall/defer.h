/*
 * neowall/defer.h — scope-bound cleanup for leak-free early returns
 * ============================================================================
 *
 * Wraps GCC/Clang's `__attribute__((cleanup))` into readable macros. A variable
 * declared with one of these runs its cleanup function automatically when it
 * goes out of scope — including on every early `return`, `break`, or `goto`.
 * This collapses the deeply-nested "if (!x) { free(a); fclose(f); return; }"
 * ladders (see config.c) into linear, single-exit code.
 *
 * Supported by GCC and Clang (the compilers neowall already requires). If you
 * ever need to port to a compiler without the extension, NW_HAVE_DEFER is 0 and
 * the macros become no-ops — code using them must then still free manually, so
 * treat these as an ergonomic win on supported toolchains, not a correctness
 * crutch the logic depends on.
 *
 * Usage:
 *
 *   nw_result load(const char *path) {
 *       NW_DEFER_FILE FILE *f = fopen(path, "rb");      // auto-fclose
 *       if (!f) return nw_err(NW_ERR_IO, "open failed");
 *
 *       NW_DEFER_FREE char *buf = malloc(size);          // auto-free
 *       if (!buf) return nw_err(NW_ERR_OOM, "buf");
 *
 *       ... use f, buf, return early anywhere ...        // both cleaned up
 *       return nw_ok();
 *   }
 *
 * Notes:
 *   - A pointer cleaned up by NW_DEFER_FREE must be the *owning* pointer. Set it
 *     to NULL if you transfer ownership out (free(NULL) is a no-op).
 *   - These are for local, scope-bound resources. Long-lived resources owned by
 *     a struct still use explicit *_destroy functions.
 */
#ifndef NEOWALL_DEFER_H
#define NEOWALL_DEFER_H

#include <stdio.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define NW_HAVE_DEFER 1
#else
#define NW_HAVE_DEFER 0
#endif

#if NW_HAVE_DEFER

static inline void nw_cleanup_free(void *p) {
    /* p is the address of the pointer variable. */
    void **pp = (void **)p;
    free(*pp);
}

static inline void nw_cleanup_fclose(FILE **f) {
    if (*f) {
        fclose(*f);
    }
}

/* Declare a heap pointer that is free()d at end of scope. */
#define NW_DEFER_FREE  __attribute__((cleanup(nw_cleanup_free)))

/* Declare a FILE* that is fclose()d at end of scope (if non-NULL). */
#define NW_DEFER_FILE  __attribute__((cleanup(nw_cleanup_fclose)))

/*
 * NW_DEFER(fn) — attach an arbitrary single-argument cleanup function to a
 * variable. `fn` receives the address of the variable.
 *
 *   NW_DEFER(close_fd) int fd = open(...);
 *   static inline void close_fd(int *fd) { if (*fd >= 0) close(*fd); }
 */
#define NW_DEFER(fn)   __attribute__((cleanup(fn)))

#else  /* no cleanup attribute */

#define NW_DEFER_FREE
#define NW_DEFER_FILE
#define NW_DEFER(fn)

#endif /* NW_HAVE_DEFER */

#endif /* NEOWALL_DEFER_H */
