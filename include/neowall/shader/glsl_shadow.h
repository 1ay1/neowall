/* glsl_shadow — detect which identifiers a user's GLSL source defines at top
 * level, so neowall's injected standard library can step out of the way.
 *
 * neowall prepends a GLSL standard library to every shader it compiles. That
 * library offers a handful of friendly, unprefixed names (`sdBox`, `pulse`,
 * `beat`, …) because unmodified Shadertoy shaders expect to be able to just
 * use them. But those same names are exactly the ones a raymarching shader is
 * most likely to define for itself — and in GLSL a second definition with an
 * identical signature is a hard compile error, not a shadow:
 *
 *     0:86(7): error: function `sdBox' redefined
 *
 * So before injecting, we scan the user's source for the names it defines and
 * suppress the colliding aliases. The rule is deliberately simple and easy to
 * explain: *if you define a name, neowall yields that name to you entirely* —
 * every overload of it, not just the one whose signature matched.
 *
 * The scanner is comment-aware (a `sdBox` mentioned in a line comment or a
 * block comment must not count) and only considers brace-depth-zero
 * definitions, so a call inside a function body is never mistaken for one.
 *
 * All of this is pure string work with no GL dependency, so it is unit-tested
 * headlessly in CI (see tests/test_glsl_shadow.c).
 */

#ifndef NEOWALL_GLSL_SHADOW_H
#define NEOWALL_GLSL_SHADOW_H

#include <stdbool.h>
#include <stddef.h>

/* A set of top-level identifiers defined by user GLSL. */
typedef struct glsl_shadow_set glsl_shadow_set;

/* Create an empty set. Returns NULL on allocation failure. */
glsl_shadow_set *glsl_shadow_create(void);

/* Scan `source` for top-level function definitions, function prototypes,
 * function-like macros, and global variable declarations, adding each name
 * found to `set`. Safe to call repeatedly to accumulate several sources (a
 * multipass shader's Common block plus the pass body, say).
 *
 * `source` may be NULL, in which case this is a no-op. Returns false only on
 * allocation failure; a source that defines nothing is a successful no-op.
 */
bool glsl_shadow_scan(glsl_shadow_set *set, const char *source);

/* True if `name` was defined by one of the scanned sources. */
bool glsl_shadow_contains(const glsl_shadow_set *set, const char *name);

/* Number of distinct names collected (used by tests and -v diagnostics). */
size_t glsl_shadow_count(const glsl_shadow_set *set);

/* Name at `index`, or NULL if out of range. Iteration order is unspecified. */
const char *glsl_shadow_at(const glsl_shadow_set *set, size_t index);

void glsl_shadow_free(glsl_shadow_set *set);

#endif /* NEOWALL_GLSL_SHADOW_H */
