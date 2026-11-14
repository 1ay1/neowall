/* capture: a helper process read on a thread. Start, stop, restart; a reader
 * that won't stop is abandoned without breaking the next run. */

#include <dirent.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "capture.h"
#include "neowall/core/log.h"

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int count_fds(void) {
    int n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

static int count_children(void) {
    char path[64], b[64];
    snprintf(path, sizeof(path), "/proc/self/task/%d/children", (int)getpid());
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    while (fscanf(f, "%63s", b) == 1) n++;
    fclose(f);
    return n;
}

static double ms_since(const struct timespec *t0) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0->tv_sec) * 1e3 + (double)(t.tv_nsec - t0->tv_nsec) / 1e6;
}

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* Reads until told to stop, counting bytes. */
static atomic_long g_bytes;
static void read_all(capture_run *r, int fd, void *ctx) {
    (void)ctx;
    capture_set_live(r, true);
    char buf[4096];
    while (capture_poll(r, fd)) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        atomic_fetch_add(&g_bytes, n);
    }
}

/* Ignores stop for a while: the abandon path. */
static atomic_bool g_release;
static void read_stuck(capture_run *r, int fd, void *ctx) {
    (void)fd; (void)ctx;
    capture_set_live(r, true);
    while (!atomic_load(&g_release)) sleep_ms(5);
}

int main(void) {
    log_set_level(LOG_LEVEL_ERROR);
    int fds = count_fds();

    static char *const yes_argv[] = {"yes", NULL};
    capture c = {.name = "test", .argv = yes_argv, .read = read_all};

    /* start, data flows, stop is quick and leaves nothing */
    for (int i = 0; i < 5; i++) {
        CHECK(capture_start(&c), "start %d", i);
        CHECK(capture_start(&c), "start again is a no-op");
        for (int w = 0; w < 200 && !capture_live(&c); w++) sleep_ms(2);
        CHECK(capture_live(&c), "live %d", i);
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        CHECK(capture_stop(&c, 1000), "stop %d", i);
        CHECK(ms_since(&t0) < 500, "stop %d took %.0f ms", i, ms_since(&t0));
        CHECK(!capture_live(&c) && !capture_running(&c), "stopped %d", i);
    }
    CHECK(atomic_load(&g_bytes) > 0, "data was read");
    CHECK(capture_stop(&c, 10), "stop when stopped is fine");
    CHECK(count_children() == 0, "no children left");
    CHECK(count_fds() == fds, "no fds leaked (%d -> %d)", fds, count_fds());

    /* a program that isn't there: the run ends by itself */
    static char *const none_argv[] = {"neowall-no-such-program", NULL};
    capture missing = {.name = "missing", .argv = none_argv, .read = read_all};
    CHECK(capture_start(&missing), "start a missing program");
    sleep_ms(50);
    CHECK(!capture_live(&missing), "never live");
    CHECK(capture_stop(&missing, 1000), "stops cleanly");

    /* a reader that won't stop is abandoned within the bound, and a restart
     * gets a fresh run that works while the old one is still stuck */
    capture s = {.name = "stuck", .argv = yes_argv, .read = read_stuck};
    CHECK(capture_start(&s), "start stuck");
    for (int w = 0; w < 200 && !capture_live(&s); w++) sleep_ms(2);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    CHECK(!capture_stop(&s, 100), "abandoned");
    CHECK(ms_since(&t0) < 400, "within the bound (%.0f ms)", ms_since(&t0));
    s.read = read_all;
    long before = atomic_load(&g_bytes);
    CHECK(capture_start(&s), "restart after abandon");
    for (int w = 0; w < 200 && atomic_load(&g_bytes) == before; w++) sleep_ms(2);
    CHECK(atomic_load(&g_bytes) > before, "the new run reads");
    CHECK(capture_stop(&s, 1000), "the new run stops cleanly");
    atomic_store(&g_release, true);   /* the old reader finishes and frees its run */
    for (int w = 0; w < 200 && count_children() > 0; w++) sleep_ms(5);
    CHECK(count_children() == 0, "the abandoned run reaps its child too");

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
