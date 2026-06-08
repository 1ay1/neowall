/* Multipass internal helpers — shared between shader_multipass.c (compile/
 * render) and multipass_parse.c (parser). NOT part of the public API; do not
 * include from anywhere outside src/shader/.
 *
 * These four utilities started life as static helpers in shader_multipass.c.
 * Promoting them to file-private linkage was the smallest cut that let the
 * parser move into its own translation unit without breaking the
 * compile/render path's reliance on the same string-scan primitives. */

#ifndef NEOWALL_SHADER_MULTIPASS_INTERNAL_H
#define NEOWALL_SHADER_MULTIPASS_INTERNAL_H

#include <stdbool.h>

/* Find next occurrence of `pattern` in `source`, skipping over single-line
 * (//) and multi-line (slash-star) comments. Returns NULL if not found. */
const char *mp_find_pattern(const char *source, const char *pattern);

/* Given a pointer somewhere before a function body, scan forward and return
 * the position one past the matching closing `}`. Comments and string literals
 * are skipped so braces inside them don't break the depth count. */
const char *mp_find_function_end(const char *start);

/* malloc + copy [start, end) + NUL. Returns NULL on empty/invalid range. */
char *mp_extract_substring(const char *start, const char *end);

/* malloc + copy of `s` (with NUL). Returns NULL if `s` is NULL. */
char *mp_str_dup(const char *s);

#endif /* NEOWALL_SHADER_MULTIPASS_INTERNAL_H */
