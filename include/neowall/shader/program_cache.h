/* GLSL program binary cache — see src/shader/program_cache.c.
 *
 * Usage at compile sites:
 *   uint64_t key = program_cache_key(vs_src, fs_src);
 *   if (!program_cache_load(key, &prog)) {
 *       prog = <compile from source>;
 *       program_cache_store(key, prog);
 *   }
 *
 * All functions require a current GL context. Degrades to no-op when
 * GL_ARB_get_program_binary is unavailable.
 */

#ifndef NEOWALL_PROGRAM_CACHE_H
#define NEOWALL_PROGRAM_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include "neowall/shader/platform_compat.h"

/* Hash both shader sources into a cache key (driver identity folded in
 * internally at load/store time). */
uint64_t program_cache_key(const char *vertex_src, const char *fragment_src);

/* Try to create a linked program from a cached driver binary.
 * Returns true and sets *out_program on hit. */
bool program_cache_load(uint64_t key, GLuint *out_program);

/* Snapshot a linked program's driver binary to the cache (best-effort). */
void program_cache_store(uint64_t key, GLuint program);

#endif /* NEOWALL_PROGRAM_CACHE_H */
