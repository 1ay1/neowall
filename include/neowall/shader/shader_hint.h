/* neowall/shader/shader_hint.h — "did you mean" for shader errors.
 *
 * The engine turns driver logs into file:line errors with a caret; this adds
 * the help line under them: the nearest neowall helper or uniform for a name
 * the driver says doesn't exist, or a missing ';' on the line before a syntax
 * error. GL-free. */
#ifndef NEOWALL_SHADER_HINT_H
#define NEOWALL_SHADER_HINT_H

#include <stdbool.h>
#include <stddef.h>

/* Closest nw* helper or i* uniform in the injected GLSL to `name`, or NULL
 * if nothing is close (or `name` exists). */
const char *shader_hint_suggest(const char *name);

/* A help line for one driver message about `line` of `file`, or false.
 * Writes "did you mean nwFbm?" or "line 3 may be missing a ';' at the end". */
bool shader_hint(const char *message, const char *file, int line, char *out, size_t cap);

#endif /* NEOWALL_SHADER_HINT_H */
