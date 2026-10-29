/* neowall/shader/program_cache.h — linked GL programs cached on disk.
 *
 * Driver compiles dominate a shader switch (50-300 ms for a raymarcher, more
 * on NVIDIA's first run). GL_ARB_get_program_binary snapshots the linked
 * program; the next run loads it in about a millisecond. Keyed by the exact
 * shader text and the driver's identity, so a driver update or an edit is a
 * miss, never a wrong program. Degrades to "always miss" without the
 * extension. Needs a current GL context. */
#ifndef NEOWALL_PROGRAM_CACHE_H
#define NEOWALL_PROGRAM_CACHE_H

#include <stdbool.h>
#include <stdint.h>

uint64_t program_cache_key(const char *vs, const char *fs);
/* A linked program, or 0 on a miss. */
unsigned program_cache_load(uint64_t key);
/* Save a linked program. Call after linking with the retrievable hint set. */
void program_cache_store(uint64_t key, unsigned prog);
/* Counters since start, for tests and `status`. */
void program_cache_stats(unsigned *hits, unsigned *misses);

#endif /* NEOWALL_PROGRAM_CACHE_H */
