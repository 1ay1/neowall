/*
 * neowall/result.h — composable error handling for C
 * ============================================================================
 *
 * Replaces the `return false; // and log the reason somewhere` idiom with a
 * small, value-typed result that carries WHY a call failed. Failure modes
 * become assertable in tests and propagatable across call boundaries, instead
 * of being lost to a log line.
 *
 * Design:
 *   - nw_status is a flat enum of failure CATEGORIES (not per-call-site codes).
 *     Keep it small; it is meant to be switched on, not exhaustively unique.
 *   - nw_result pairs a status with an optional static context string. The
 *     string MUST have static storage duration (a literal or a const char* that
 *     outlives the result) — nw_result never owns or frees it. This keeps the
 *     type trivially copyable and allocation-free on the hot path.
 *   - NW_OK is the zero value, so `nw_result r = {0};` is success and a
 *     zero-initialised struct field defaults to "no error".
 *
 * Conventions:
 *   - Functions that can fail for >1 reason return nw_result.
 *   - Functions whose only failure is "bad argument / out of memory" and that
 *     have nothing useful to say MAY keep returning bool — don't ceremony-wrap
 *     trivial predicates.
 *   - Use NW_TRY to short-circuit: it returns the result if it is an error.
 */
#ifndef NEOWALL_RESULT_H
#define NEOWALL_RESULT_H

#include <stdbool.h>

typedef enum nw_status {
    NW_OK = 0,             /* Success. Must stay 0. */
    NW_ERR_INVALID_ARG,   /* Caller passed NULL / out-of-range / nonsense. */
    NW_ERR_OOM,           /* Allocation failed. */
    NW_ERR_IO,            /* read/write/open/socket failure (see errno at site). */
    NW_ERR_NOT_FOUND,     /* Requested resource does not exist. */
    NW_ERR_PARSE,         /* Malformed input (config, shader, etc.). */
    NW_ERR_UNSUPPORTED,   /* Backend/platform cannot do this. */
    NW_ERR_BACKEND,       /* Compositor/display-server-level failure. */
    NW_ERR_GL,            /* EGL/OpenGL call failed. */
    NW_ERR_STATE,         /* Operation invalid for current object state. */
    NW_ERR_AGAIN,         /* Transient; retry may succeed (EAGAIN-like). */
} nw_status;

typedef struct nw_result {
    nw_status status;
    const char *context;  /* Static string, never owned. May be NULL. */
} nw_result;

/* ----- Constructors ----- */

static inline nw_result nw_ok(void) {
    return (nw_result){.status = NW_OK, .context = NULL};
}

/* `ctx` must be a string literal or otherwise have static lifetime. */
static inline nw_result nw_err(nw_status status, const char *ctx) {
    return (nw_result){.status = status, .context = ctx};
}

/* ----- Predicates ----- */

static inline bool nw_is_ok(nw_result r)  { return r.status == NW_OK; }
static inline bool nw_is_err(nw_result r) { return r.status != NW_OK; }

/* ----- Human-readable category name (for logs / test diagnostics) ----- */

static inline const char *nw_status_str(nw_status s) {
    switch (s) {
        case NW_OK:              return "ok";
        case NW_ERR_INVALID_ARG: return "invalid-argument";
        case NW_ERR_OOM:         return "out-of-memory";
        case NW_ERR_IO:          return "io-error";
        case NW_ERR_NOT_FOUND:   return "not-found";
        case NW_ERR_PARSE:       return "parse-error";
        case NW_ERR_UNSUPPORTED: return "unsupported";
        case NW_ERR_BACKEND:     return "backend-error";
        case NW_ERR_GL:          return "gl-error";
        case NW_ERR_STATE:       return "invalid-state";
        case NW_ERR_AGAIN:       return "try-again";
    }
    return "unknown";
}

/*
 * NW_TRY(expr) — evaluate an nw_result-returning expression and propagate the
 * error to the caller if it failed. Used inside functions that themselves
 * return nw_result, to flatten the `if (err) return err;` ladder.
 *
 *   nw_result load(void) {
 *       NW_TRY(open_config());
 *       NW_TRY(parse_config());
 *       return nw_ok();
 *   }
 */
#define NW_TRY(expr)                                                            \
    do {                                                                       \
        nw_result _nw_try_r = (expr);                                          \
        if (_nw_try_r.status != NW_OK) {                                       \
            return _nw_try_r;                                                  \
        }                                                                      \
    } while (0)

#endif /* NEOWALL_RESULT_H */
