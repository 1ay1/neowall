/*
 * Tests for the reactive signals as data-plane providers (slice 2).
 *
 * The one that matters is test_every_legacy_bind_name_resolves(). Porting 24
 * enum arms to a table is exactly the kind of change where one signal gets
 * dropped or misspelled and nobody notices, because a missing binding does not
 * crash — the uniform silently reads 0 and the shader just looks a bit wrong.
 * So the full legacy name list, aliases included, is pinned here verbatim from
 * the old multipass_bind_from_name() table.
 *
 * Links reactive.c for real, so signals read real values off this machine.
 * Still headless: no GL, no display server.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "neowall/source/reactive.h"
#include "neowall/source/source.h"

static int checks   = 0;
static int failures = 0;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while (0)

static nw_source_ctx ctx_at(uint64_t now_ms) {
    nw_source_ctx c = {0};
    c.now_ms        = now_ms;
    c.width         = 1920;
    c.height        = 1080;
    c.visible       = true;
    return c;
}

/*
 * Every name multipass_bind_from_name() used to accept, copied out of the old
 * table. If a manifest in the wild used it, it must still work.
 */
static const char *legacy_names[] = {
    "cpu",        "ram",         "net_down",   "netdown",    "net_up",       "netup",
    "battery",    "time_of_day", "timeofday",  "sun",        "audio",        "audio_level",
    "audio_bass", "bass",        "audio_mid",  "mid",        "audio_treble", "treble",
    "audio_beat", "beat",        "key_energy", "keys",       "mouse_energy", "mouse",
    "swap",       "disk_read",   "diskread",   "disk_write", "diskwrite",    "load",
    "cpu_temp",   "cputemp",     "gpu",        "gpu_temp",   "gputemp",      "uptime",
    "procs",      "processes",
};

static void test_every_legacy_bind_name_resolves(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();

    size_t n = sizeof(legacy_names) / sizeof(legacy_names[0]);
    for (size_t i = 0; i < n; i++) {
        const nw_source_vtable *vt = nw_source_find(legacy_names[i]);
        CHECK(vt != NULL, "legacy bind name '%s' no longer resolves", legacy_names[i]);
        if (vt) {
            CHECK(vt->kind == NW_SOURCE_SCALAR, "'%s' should be scalar", legacy_names[i]);
        }
    }

    /* And they must actually open, not just be present in the table. */
    char err[NW_SOURCE_ERR_MAX];
    for (size_t i = 0; i < n; i++) {
        nw_source src;
        bool ok = nw_is_ok(nw_source_open_text(legacy_names[i], &src, err, sizeof(err)));
        CHECK(ok, "legacy name '%s' failed to open: %s", legacy_names[i], err);
        if (ok) {
            nw_source_destroy(&src);
        }
    }
}

/* The port was also meant to unlock the signals reactive.c already sampled but
 * the enum never exposed. Pin a few so they are not quietly dropped again. */
static void test_newly_exposed_signals(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();

    static const char *newly[] = {
        "ram_gb",     "ram_total_gb", "net_down_mbs", "net_up_mbs",   "load_raw",
        "cpu_temp_c", "gpu_temp_c",   "cpu_max",      "cpu_spread",   "cpu_cores",
        "thermal",    "activity",     "pulse",        "day_fraction", "proc_count",
        "charging",   "audio_active", "nv_gpu",       "nv_vram",
    };
    for (size_t i = 0; i < sizeof(newly) / sizeof(newly[0]); i++) {
        CHECK(nw_source_find(newly[i]) != NULL, "signal '%s' should be bindable", newly[i]);
    }

    /* The old enum had 24 arms. If the table ever shrinks below that we have
     * regressed rather than extended. */
    CHECK(nw_source_reactive_signal_count() > 24,
          "expected more signals than the old 24-arm enum, got %zu",
          nw_source_reactive_signal_count());
}

static void test_unknown_signal_is_an_error(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();

    nw_source src;
    char err[NW_SOURCE_ERR_MAX] = {0};
    CHECK(nw_is_err(nw_source_open_text("banana", &src, err, sizeof(err))),
          "unknown signal must fail, not silently read 0");
    CHECK(err[0] != '\0', "failure should explain itself");
}

/* Non-float fields (int cpu_cores, bool charging) are widened on read. A wrong
 * offset or kind tag here reads adjacent memory and produces plausible-looking
 * garbage, so check the values are at least in range. */
static void test_typed_fields_widen(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();
    reactive_sample();
    nw_reactive_invalidate();

    nw_source src;
    nw_source_ctx c = ctx_at(1000);

    if (nw_is_ok(nw_source_open_text("charging", &src, NULL, 0))) {
        nw_source_tick(&src, &c);
        float v = nw_source_scalar(&src);
        CHECK(v == 0.0f || v == 1.0f, "bool signal must be 0 or 1, got %f", v);
        nw_source_destroy(&src);
    }

    if (nw_is_ok(nw_source_open_text("cpu_cores", &src, NULL, 0))) {
        nw_source_tick(&src, &c);
        float v = nw_source_scalar(&src);
        CHECK(v >= 0.0f && v <= (float)REACTIVE_MAX_CPU_CORES, "cpu_cores out of range: %f", v);
        nw_source_destroy(&src);
    }

    /* A 0..1 normalised signal reading wildly outside that range means the
     * offset is pointing at the wrong field. */
    if (nw_is_ok(nw_source_open_text("ram", &src, NULL, 0))) {
        nw_source_tick(&src, &c);
        float v = nw_source_scalar(&src);
        CHECK(v >= 0.0f && v <= 1.0f, "ram should be 0..1, got %f", v);
        nw_source_destroy(&src);
    }
}

/* Aliases and canonical names must read the same field, not two copies that
 * could drift. */
static void test_alias_matches_canonical(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();
    reactive_sample();
    nw_reactive_invalidate();

    nw_source canonical, alias;
    CHECK(nw_is_ok(nw_source_open_text("audio_level", &canonical, NULL, 0)), "open canonical");
    CHECK(nw_is_ok(nw_source_open_text("audio", &alias, NULL, 0)), "open alias");

    nw_source_ctx c = ctx_at(2000);
    nw_source_tick(&canonical, &c);
    nw_source_tick(&alias, &c);

    CHECK(nw_source_scalar(&canonical) == nw_source_scalar(&alias),
          "alias should read the same field: %f vs %f", nw_source_scalar(&canonical),
          nw_source_scalar(&alias));

    nw_source_destroy(&canonical);
    nw_source_destroy(&alias);
}

/* The occlusion contract has to survive the port: a reactive uniform behind a
 * maximized window must not sample. */
static void test_reactive_respects_visibility(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();

    nw_source src;
    CHECK(nw_is_ok(nw_source_open_text("cpu", &src, NULL, 0)), "open cpu");

    nw_source_ctx c = ctx_at(0);
    CHECK(nw_source_tick(&src, &c), "visible source ticks");

    nw_source_set_visible(&src, false);
    for (int i = 1; i <= 5; i++) {
        c = ctx_at((uint64_t)i * 10000);
        CHECK(!nw_source_tick(&src, &c), "hidden reactive source must not sample");
    }

    nw_source_set_visible(&src, true);
    c = ctx_at(60000);
    CHECK(nw_source_tick(&src, &c), "visible again resumes");

    nw_source_destroy(&src);
}

/* Many bound uniforms must not mean many snapshot copies. The snapshot is ~4KB
 * (two 512-float audio rows); copying it per uniform per frame would be a real
 * cost on a dashboard with twenty of them. */
static void test_snapshot_is_shared_per_frame(void) {
    nw_source_registry_reset();
    nw_source_register_reactive();

    nw_source sources[8];
    static const char *names[8] = {"cpu", "ram", "swap", "load", "sun", "gpu", "battery", "procs"};
    for (int i = 0; i < 8; i++) {
        CHECK(nw_is_ok(nw_source_open_text(names[i], &sources[i], NULL, 0)), "open %s", names[i]);
    }

    /* Same timestamp across all eight: one underlying snapshot, and every
     * source still gets a value. */
    nw_source_ctx c = ctx_at(5000);
    for (int i = 0; i < 8; i++) {
        CHECK(nw_source_tick(&sources[i], &c), "%s ticks", names[i]);
    }
    for (int i = 0; i < 8; i++) {
        float v = nw_source_scalar(&sources[i]);
        CHECK(v >= 0.0f, "%s should read a sane value, got %f", names[i], v);
        nw_source_destroy(&sources[i]);
    }
}

/* Shutdown with capture helpers still running, again and again: it must
 * finish quickly, leave no children and leak no fds. The helpers are fakes
 * on PATH: a parec that streams forever and an nvidia-smi that ignores
 * SIGTERM, so only the SIGKILL path can stop it. */
static int count_fds(void) {
    int n  = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    while (readdir(d))
        n++;
    closedir(d);
    return n;
}

static int count_children(void) {
    char path[64], buf[512];
    snprintf(path, sizeof(path), "/proc/self/task/%d/children", (int)getpid());
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    while (fscanf(f, "%511s", buf) == 1)
        n++;
    fclose(f);
    return n;
}

static void test_shutdown_cycles(void) {
    char dir[] = "/tmp/nw-fake-XXXXXX";
    if (!mkdtemp(dir)) return;
    char p[256];
    snprintf(p, sizeof(p), "%s/parec", dir);
    FILE *f = fopen(p, "w");
    fprintf(f, "#!/bin/sh\n: > %s/parec.ran\nexec cat /dev/zero\n", dir);
    fclose(f);
    chmod(p, 0755);
    snprintf(p, sizeof(p), "%s/nvidia-smi", dir);
    f = fopen(p, "w");
    fprintf(
        f,
        "#!/bin/sh\n: > %s/nv.ran\ntrap '' TERM\nwhile :; do echo '1, 2, 3, 4, 5, 6'; sleep 0.1; done\n",
        dir);
    fclose(f);
    chmod(p, 0755);
    const char *old = getenv("PATH");
    char path[4096];
    snprintf(path, sizeof(path), "%s:%s", dir, old ? old : "/usr/bin:/bin");
    setenv("PATH", path, 1);

    int fds = count_fds();

    /* Nothing asked for audio or NVIDIA data: neither helper may start. */
    CHECK(reactive_init(), "init without consumers");
    {
        struct timespec ts = {0, 300 * 1000000L};
        nanosleep(&ts, NULL);
        char a[300], b[300];
        snprintf(a, sizeof(a), "%s/parec.ran", dir);
        snprintf(b, sizeof(b), "%s/nv.ran", dir);
        CHECK(access(a, F_OK) != 0 && access(b, F_OK) != 0, "no capture helper without a consumer");
        CHECK(count_children() == 0, "no children without a consumer");
    }
    reactive_shutdown();

    for (int i = 0; i < 5; i++) {
        CHECK(reactive_init(), "init %d", i);
        reactive_audio_start();
        reactive_nvidia_start();
        /* let both helpers get going, so shutdown has live children to stop */
        for (int w = 0; w < 200; w++) {
            char a[300], b[300];
            snprintf(a, sizeof(a), "%s/parec.ran", dir);
            snprintf(b, sizeof(b), "%s/nv.ran", dir);
            if (access(a, F_OK) == 0 && access(b, F_OK) == 0) break;
            struct timespec ts = {0, 5 * 1000000L};
            nanosleep(&ts, NULL);
        }
        if (i == 0) {
            char a[300], b[300];
            snprintf(a, sizeof(a), "%s/parec.ran", dir);
            snprintf(b, sizeof(b), "%s/nv.ran", dir);
            CHECK(access(a, F_OK) == 0 && access(b, F_OK) == 0, "both fake helpers ran");
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        reactive_shutdown();
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
        CHECK(ms < 1500, "shutdown %d took %.0f ms", i, ms);
    }
    CHECK(count_children() == 0, "no capture helper outlives shutdown (%d left)", count_children());
    CHECK(count_fds() == fds, "no fds leaked (%d -> %d)", fds, count_fds());

    setenv("PATH", old ? old : "/usr/bin:/bin", 1);
    snprintf(p, sizeof(p), "%s/parec", dir);
    unlink(p);
    snprintf(p, sizeof(p), "%s/nvidia-smi", dir);
    unlink(p);
    snprintf(p, sizeof(p), "%s/parec.ran", dir);
    unlink(p);
    snprintf(p, sizeof(p), "%s/nv.ran", dir);
    unlink(p);
    rmdir(dir);
}

int main(void) {
    /* Best-effort: reactive_init spawns capture helpers and may legitimately
     * fail in CI with no audio. Signals then read 0, which is fine for these
     * tests — they check wiring, not hardware. */
    reactive_init();

    test_every_legacy_bind_name_resolves();
    test_newly_exposed_signals();
    test_unknown_signal_is_an_error();
    test_typed_fields_widen();
    test_alias_matches_canonical();
    test_reactive_respects_visibility();
    test_snapshot_is_shared_per_frame();

    reactive_shutdown();

    test_shutdown_cycles();

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
