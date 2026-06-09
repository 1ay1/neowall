/* Fisher-Yates shuffle for wallpaper cycle paths (issue #47).
 *
 * Split out of config.c so the shuffle logic can be unit-tested without
 * pulling in the vibe parser, compositor, shader, and image modules.
 *
 * Uses random() (POSIX, ~31-bit period — fine for picking a wallpaper order),
 * seeded once per process from CLOCK_MONOTONIC + getpid() so two daemons
 * started in the same second don't pick the same sequence. The seed is lazy:
 * a cycle that's never built never touches the RNG.
 *
 * `keep_first_at_zero` is the "re-shuffle on wrap" mode: it shuffles only
 * indices [1, n) so the wallpaper that was just shown stays at position 0,
 * preventing the same image appearing twice in a row across a wrap. */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "neowall/config/config.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void config_shuffle_cycle_paths(char **paths, size_t n, bool keep_first_at_zero) {
    if (!paths || n < 2) return;

    static bool seeded = false;
    if (!seeded) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        unsigned int seed = (unsigned int)ts.tv_nsec
                          ^ (unsigned int)(ts.tv_sec << 16)
                          ^ (unsigned int)getpid();
        srandom(seed);
        seeded = true;
    }

    size_t start = keep_first_at_zero ? 1 : 0;
    if (n - start < 2) return;

    /* Standard Fisher-Yates: walk from the end down, swap with a random
     * earlier (or same) slot. Uniform if random() is uniform on [0, RAND_MAX]. */
    for (size_t i = n - 1; i > start; i--) {
        size_t j = start + (size_t)(random() % (long)(i - start + 1));
        char *tmp = paths[i];
        paths[i] = paths[j];
        paths[j] = tmp;
    }
}
