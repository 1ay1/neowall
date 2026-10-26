/* The terminal's output hook: a real child on a real PTY, no display.
 *
 * An idle-gated renderer goes to sleep when the terminal settles. The only
 * thing that wakes it is on_output, called from the reader thread when the
 * child writes. If that stopped firing, a settled terminal would freeze on its
 * last frame, so pin it here. */

#define _POSIX_C_SOURCE 200809L
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "neowall/terminal/terminal.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static atomic_int g_calls;
static atomic_uintptr_t g_ctx;

static void on_output(void *ctx) {
    atomic_fetch_add(&g_calls, 1);
    atomic_store(&g_ctx, (uintptr_t)ctx);
}

static void sleep_ms(int ms) {
    struct timespec d = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&d, NULL);
}

/* Wait until `pred` or the deadline. */
static bool wait_for(int min_calls, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        if (atomic_load(&g_calls) >= min_calls) return true;
        sleep_ms(10);
    }
    return atomic_load(&g_calls) >= min_calls;
}

int main(void) {
    int marker = 0;
    term_spawn_opts o = {
        .cmd = "sh -c 'printf one; sleep 0.3; printf two; sleep 30'",
        .cols = 40, .rows = 5,
        .on_output = on_output, .on_output_ctx = &marker,
    };
    terminal *t = NULL;
    nw_result r = term_spawn(&o, &t);
    CHECK(nw_is_ok(r) && t, "spawn");
    if (!t) return 1;

    CHECK(wait_for(1, 2000), "first output fires the hook");
    CHECK(atomic_load(&g_ctx) == (uintptr_t)&marker, "hook gets its ctx");
    int after_first = atomic_load(&g_calls);

    CHECK(wait_for(after_first + 1, 2000), "later output fires it again");

    /* Then the child sleeps: no output, so no more calls. */
    sleep_ms(150);
    int settled = atomic_load(&g_calls);
    sleep_ms(400);
    CHECK(atomic_load(&g_calls) == settled,
          "a quiet child doesn't fire the hook (%d -> %d)", settled, atomic_load(&g_calls));

    term_destroy(t);

    /* No hook set: spawning and output must still work. */
    term_spawn_opts plain = {.cmd = "sh -c 'printf x; sleep 30'", .cols = 10, .rows = 2};
    t = NULL;
    CHECK(nw_is_ok(term_spawn(&plain, &t)) && t, "spawn without a hook");
    if (t) {
        uint64_t e0 = term_dirty_epoch(t);
        for (int i = 0; i < 100 && term_dirty_epoch(t) == e0; i++) sleep_ms(10);
        CHECK(term_dirty_epoch(t) != e0, "output still advances the epoch with no hook");
        term_destroy(t);
    }

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
