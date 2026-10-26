#ifndef NEOWALL_SHADER_DIAG_H
#define NEOWALL_SHADER_DIAG_H

/* Turn a GL driver's shader info log into readable errors.
 *
 * Drivers all print errors differently:
 *   NVIDIA   0(17) : error C0000: syntax error, unexpected ...
 *   Mesa     0:17(5): error: `glow' undeclared
 *   others   ERROR: 0:17: 'glow' : undeclared identifier
 * shader_diag_parse() pulls out (line, column, severity, message) from any of
 * them. The line is already a file line, because the compiled source carries
 * #line directives. */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int  line;          /* 1-based file line, 0 = unknown */
    int  column;        /* 1-based, 0 = unknown */
    bool warning;       /* false = error */
    char message[256];  /* driver noise stripped ("error C1503:" etc.) */
} shader_diag;

/* Parse up to max entries out of a driver info log. Duplicate (line, message)
 * pairs are dropped. Returns the count. */
size_t shader_diag_parse(const char *log, shader_diag *out, size_t max);

/* Write a friendly report for the errors in `log` against `source` (the
 * user's file) into buf: one block per error with the file:line, the message,
 * the source line and a caret. Adds a "did you mean" for undeclared names that
 * are close to a known neowall helper or uniform. Returns bytes written, 0 if
 * nothing could be parsed (caller should fall back to the raw log). */
size_t shader_diag_format(const char *log, const char *source, const char *path,
                          bool color, char *buf, size_t size);

/* Closest known nw* helper / i* uniform to `name`, or NULL if nothing is
 * close. Known names are scanned from the GLSL passed to
 * shader_diag_set_known(). Exposed for tests. */
const char *shader_diag_suggest(const char *name);

/* Tell the suggester which GLSL neowall prepends; it collects every nw* and
 * i* name declared there. Call once; chunks must outlive the process. */
void shader_diag_set_known(const char *const *chunks, size_t n);

#endif /* NEOWALL_SHADER_DIAG_H */
