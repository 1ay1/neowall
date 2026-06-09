/* Unit tests for config_shuffle_cycle_paths (issue #47).
 *
 * Pure-data test: hands the shuffler arrays of unique string pointers, then
 * asserts the multiset is preserved (it's a permutation), that the result
 * differs from sorted input often enough to be plausibly random, and that
 * keep_first_at_zero pins slot 0. Covers degenerate sizes too.
 *
 * Note: the production shuffler seeds itself lazily from CLOCK_MONOTONIC ^
 * getpid() — the seed is fixed for the lifetime of the test process. That's
 * fine: we don't assert any particular permutation, only invariants.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/config/config.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                      \
    } while (0)

#define N 32

static void fill(char **paths, char (*storage)[16], int n) {
    for (int i = 0; i < n; i++) {
        snprintf(storage[i], 16, "item-%02d", i);
        paths[i] = storage[i];
    }
}

/* Verify `paths` is a permutation of the n distinct strings in `storage`. */
static bool is_permutation(char **paths, char (*storage)[16], int n) {
    bool *seen = calloc((size_t)n, sizeof(bool));
    if (!seen) return false;
    bool ok = true;
    for (int i = 0; i < n; i++) {
        int idx = -1;
        for (int j = 0; j < n; j++) {
            if (paths[i] == storage[j]) { idx = j; break; }
        }
        if (idx < 0 || seen[idx]) { ok = false; break; }
        seen[idx] = true;
    }
    free(seen);
    return ok;
}

static void test_degenerate(void) {
    /* n < 2 must be a no-op (and definitely not crash). */
    char *one[] = { "alpha" };
    config_shuffle_cycle_paths(one, 1, false);
    CHECK(strcmp(one[0], "alpha") == 0);

    config_shuffle_cycle_paths(NULL, 5, false);  /* NULL paths */
    config_shuffle_cycle_paths(one, 0, false);   /* zero len */
    CHECK(1);  /* survived */
}

static void test_permutation_full(void) {
    char storage[N][16];
    char *paths[N];
    fill(paths, storage, N);

    config_shuffle_cycle_paths(paths, N, false);
    CHECK(is_permutation(paths, storage, N));
}

static void test_keep_first_pins_slot_zero(void) {
    char storage[N][16];
    char *paths[N];
    fill(paths, storage, N);

    char *pinned = paths[0];
    config_shuffle_cycle_paths(paths, N, true);

    CHECK(paths[0] == pinned);
    CHECK(is_permutation(paths, storage, N));
}

static void test_keep_first_n_eq_2(void) {
    /* n == 2 with keep_first_at_zero: only paths[1..] is shuffleable, which
     * is a single element. The function must early-return without touching
     * either slot. */
    char *paths[2] = { "a", "b" };
    config_shuffle_cycle_paths(paths, 2, true);
    CHECK(strcmp(paths[0], "a") == 0);
    CHECK(strcmp(paths[1], "b") == 0);
}

/* Run many shuffles and confirm at least ONE of them differs from the sorted
 * input. With N=32 and a real PRNG the probability all 64 runs land on the
 * identity permutation is ≈ (1/32!)^64 — astronomically below false-flake
 * thresholds. If this fires, the RNG is broken or the shuffle isn't running. */
static void test_actually_shuffles(void) {
    char storage[N][16];
    char *paths[N];
    int changed_runs = 0;

    for (int run = 0; run < 64; run++) {
        fill(paths, storage, N);
        config_shuffle_cycle_paths(paths, N, false);
        for (int i = 0; i < N; i++) {
            if (paths[i] != storage[i]) { changed_runs++; break; }
        }
    }
    CHECK(changed_runs > 0);
    /* And for a sanity check the OPPOSITE: with N=32 the chance every run
     * happens to leave even one fixed point... no, fixed points are common,
     * skip. The "at least one differs" check above is the load-bearing one. */
}

int main(void) {
    test_degenerate();
    test_permutation_full();
    test_keep_first_pins_slot_zero();
    test_keep_first_n_eq_2();
    test_actually_shuffles();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
