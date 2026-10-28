/* Unit tests for the pure helper functions in src/utils.c.
 *
 * These cover the math/format/parse helpers that have no GL, Wayland, or
 * filesystem dependencies, so they can run headless in CI. The graphics and
 * compositor paths still require a live display server and are not covered
 * here — this harness exists to lock down the parts that CAN be tested
 * deterministically.
 *
 * utils.c calls log_*() and references NEOWALL_VERSION; tests/stubs.c provides
 * those so we can link utils.c in isolation without the full daemon.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

/* Prototypes from src/utils.c (kept in sync with include/neowall.h). */
float lerp(float a, float b, float t);
float clamp(float value, float min, float max);
float ease_in_out_cubic(float t);
void format_bytes(uint64_t bytes, char *buf, size_t size);
bool expand_path(const char *path, char *expanded, size_t size);
const char *neowall_secure_runtime_dir(void);
bool neowall_parse_index(const char *s, long *out);
bool write_wallpaper_state(const char *output_name, const char *wallpaper_path,
                           const char *mode, int cycle_index, int cycle_total,
                           const char *status);
int restore_cycle_index_from_state(const char *output_name);
bool remove_wallpaper_state(const char *output_name);
bool prune_wallpaper_state(const char *const *output_names, size_t output_count);
bool neowall_find_data(const char *rel, char *out, size_t size);

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

#define CHECK_FEQ(a, b)                                                        \
    do {                                                                       \
        checks++;                                                              \
        if (fabsf((float)(a) - (float)(b)) > 1e-5f) {                          \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s (%.6f) != %s (%.6f)\n", __FILE__,  \
                    __LINE__, #a, (double)(a), #b, (double)(b));               \
        }                                                                      \
    } while (0)

static void test_lerp(void) {
    CHECK_FEQ(lerp(0.0f, 10.0f, 0.0f), 0.0f);
    CHECK_FEQ(lerp(0.0f, 10.0f, 1.0f), 10.0f);
    CHECK_FEQ(lerp(0.0f, 10.0f, 0.5f), 5.0f);
    CHECK_FEQ(lerp(-4.0f, 4.0f, 0.25f), -2.0f);
}

static void test_clamp(void) {
    CHECK_FEQ(clamp(5.0f, 0.0f, 10.0f), 5.0f);
    CHECK_FEQ(clamp(-1.0f, 0.0f, 10.0f), 0.0f);
    CHECK_FEQ(clamp(11.0f, 0.0f, 10.0f), 10.0f);
    CHECK_FEQ(clamp(0.0f, 0.0f, 0.0f), 0.0f);
}

static void test_ease(void) {
    /* Endpoints pinned, midpoint symmetric, monotonic non-decreasing. */
    CHECK_FEQ(ease_in_out_cubic(0.0f), 0.0f);
    CHECK_FEQ(ease_in_out_cubic(1.0f), 1.0f);
    CHECK_FEQ(ease_in_out_cubic(0.5f), 0.5f);
    float prev = ease_in_out_cubic(0.0f);
    for (int i = 1; i <= 20; i++) {
        float t = (float)i / 20.0f;
        float v = ease_in_out_cubic(t);
        CHECK(v >= prev - 1e-5f);
        prev = v;
    }
}

static void test_format_bytes(void) {
    char buf[64];
    format_bytes(512, buf, sizeof(buf));
    CHECK(strcmp(buf, "512 B") == 0);
    format_bytes(1024, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.00 KB") == 0);
    format_bytes(1024ULL * 1024ULL, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.00 MB") == 0);
    format_bytes(1536, buf, sizeof(buf));
    CHECK(strcmp(buf, "1.50 KB") == 0);
}

static void test_expand_path(void) {
    char out[256];

    /* No tilde: copied verbatim, NUL-terminated. */
    CHECK(expand_path("/etc/neowall/config", out, sizeof(out)));
    CHECK(strcmp(out, "/etc/neowall/config") == 0);

    /* Tilde expansion against a known HOME. */
    setenv("HOME", "/home/test", 1);
    CHECK(expand_path("~/wp.png", out, sizeof(out)));
    CHECK(strcmp(out, "/home/test/wp.png") == 0);

    /* Overflow is rejected, not truncated into a buffer overrun. */
    char tiny[4];
    CHECK(!expand_path("/way/too/long/for/four/bytes", tiny, sizeof(tiny)));

    /* Defensive: NULL args rejected. */
    CHECK(!expand_path(NULL, out, sizeof(out)));
    CHECK(!expand_path("/x", NULL, sizeof(out)));
    CHECK(!expand_path("/x", out, 0));
}

/* neowall_parse_index(): the checked replacement for atoi() on user- and
 * disk-supplied index strings. The case that atoi() gets wrong is the one at
 * the bottom: an all-digit string too large for an int. */
static void test_parse_index(void) {
    long v;

    /* Plain non-negative decimals. */
    v = -1;
    CHECK(neowall_parse_index("0", &v) && v == 0);
    v = -1;
    CHECK(neowall_parse_index("7", &v) && v == 7);
    v = -1;
    CHECK(neowall_parse_index("0042", &v) && v == 42);

    /* Not a number, or not entirely a number. */
    v = -1;
    CHECK(!neowall_parse_index("", &v));
    CHECK(!neowall_parse_index("abc", &v));
    CHECK(!neowall_parse_index("3x", &v));
    CHECK(!neowall_parse_index("3 ", &v));
    CHECK(!neowall_parse_index(" 3", &v));
    CHECK(!neowall_parse_index("+3", &v));
    CHECK(!neowall_parse_index("-1", &v));
    CHECK(!neowall_parse_index("0x10", &v));
    CHECK(!neowall_parse_index(NULL, &v));
    CHECK(v == -1);   /* nothing written on failure */

    /* Overflow: all digits, but far past what an int (or a long) can hold.
     * atoi("99999999999999999999") is undefined behaviour; in practice glibc
     * saturates and truncates, handing the caller a bogus index. */
    CHECK(!neowall_parse_index("99999999999999999999", &v));

    /* LONG_MAX itself parses; LONG_MAX + 1 does not. */
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", LONG_MAX);
    v = -1;
    CHECK(neowall_parse_index(buf, &v) && v == LONG_MAX);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)LONG_MAX + 1UL);
    CHECK(!neowall_parse_index(buf, &v));

#if LONG_MAX > INT_MAX
    /* INT_MAX + 1: fits in a long, so the parse succeeds and the caller is the
     * one that must reject it - which is exactly why the value is reported
     * rather than silently squeezed into an int, as atoi() would. */
    snprintf(buf, sizeof(buf), "%ld", (long)INT_MAX + 1);
    v = -1;
    CHECK(neowall_parse_index(buf, &v) && v == (long)INT_MAX + 1);
#endif
}

/* neowall_secure_runtime_dir(): must create a private 0700 directory we own
 * under XDG_RUNTIME_DIR, and must REFUSE a name already taken by a symlink
 * (the /tmp squat attack the hardening closes). */
static void test_secure_runtime_dir(void) {
    char base[256];
    snprintf(base, sizeof(base), "/tmp/nw-test-%d-%ld", (int)getpid(), (long)time(NULL));
    if (mkdir(base, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "SKIP secure_runtime_dir: cannot mkdir %s\n", base);
        return;
    }
    setenv("XDG_RUNTIME_DIR", base, 1);

    /* Happy path: returns <base>/neowall, created 0700, owned by us. */
    const char *dir = neowall_secure_runtime_dir();
    CHECK(dir != NULL);
    if (dir) {
        struct stat st;
        CHECK(lstat(dir, &st) == 0);
        CHECK(S_ISDIR(st.st_mode));
        CHECK((st.st_mode & (S_IRWXG | S_IRWXO)) == 0);   /* no group/other access */
        CHECK(st.st_uid == getuid());
    }

    /* Attack: replace the XDG_RUNTIME_DIR/neowall path with a symlink. The
     * helper must NOT return that symlinked path. It is allowed to fall back
     * to the ownership-checked /tmp/neowall-<uid> dir, but whatever it returns
     * must be a real directory we own and never the planted symlink. */
    char sub[300];
    snprintf(sub, sizeof(sub), "%s/neowall", base);
    rmdir(sub);
    if (symlink("/tmp", sub) == 0) {
        const char *d2 = neowall_secure_runtime_dir();
        /* Must not be the symlinked path itself. */
        CHECK(d2 == NULL || strcmp(d2, sub) != 0);
        if (d2) {
            struct stat st;
            CHECK(lstat(d2, &st) == 0 && S_ISDIR(st.st_mode));   /* real dir, not symlink */
            CHECK(st.st_uid == getuid());
        }
        unlink(sub);
    }
    rmdir(base);
}

static void test_persisted_state_pruning(void) {
    char base[256];
    snprintf(base, sizeof(base), "/tmp/nw-state-test-%d-%ld",
             (int)getpid(), (long)time(NULL));
    CHECK(mkdir(base, 0700) == 0);
    setenv("XDG_STATE_HOME", base, 1);

    CHECK(write_wallpaper_state("DP-1", "/one.png", "fill", 1, 3, "active"));
    CHECK(write_wallpaper_state("HDMI-1", "/two.png", "fit", 2, 4, "active"));
    CHECK(remove_wallpaper_state("DP-1"));
    CHECK(restore_cycle_index_from_state("DP-1") == 0);
    CHECK(restore_cycle_index_from_state("HDMI-1") == 2);

    CHECK(write_wallpaper_state("DP-2", "/three.png", "fill", 3, 5, "active"));
    const char *keep[] = {"DP-2"};
    CHECK(prune_wallpaper_state(keep, 1));
    CHECK(restore_cycle_index_from_state("HDMI-1") == 0);
    CHECK(restore_cycle_index_from_state("DP-2") == 3);
    CHECK(remove_wallpaper_state("missing"));

    char state_dir[300];
    char state_file[320];
    snprintf(state_dir, sizeof(state_dir), "%s/neowall", base);
    snprintf(state_file, sizeof(state_file), "%s/state", state_dir);
    unlink(state_file);
    rmdir(state_dir);
    rmdir(base);
    unsetenv("XDG_STATE_HOME");
}

static void touch(const char *path) {
    FILE *f = fopen(path, "w");
    if (f) fclose(f);
}

/* neowall_find_data(): $XDG_DATA_HOME first, then $XDG_DATA_DIRS in order,
 * relative entries ignored, missing files reported as not found. */
static void test_find_data(void) {
    char base[256];
    snprintf(base, sizeof(base), "/tmp/nw-data-test-%d-%ld", (int)getpid(), (long)time(NULL));
    char d[1200], p[1200], out[1200];
    const char *subs[] = {"home", "home/neowall", "a", "a/neowall", "a/neowall/shaders",
                          "b", "b/neowall", "b/neowall/shaders"};
    mkdir(base, 0700);
    for (size_t i = 0; i < sizeof(subs) / sizeof(subs[0]); i++) {
        snprintf(d, sizeof(d), "%s/%s", base, subs[i]);
        mkdir(d, 0700);
    }
    snprintf(p, sizeof(p), "%s/a/neowall/shaders/x.glsl", base); touch(p);
    snprintf(p, sizeof(p), "%s/b/neowall/shaders/x.glsl", base); touch(p);
    snprintf(p, sizeof(p), "%s/b/neowall/shaders/only_b.glsl", base); touch(p);
    snprintf(p, sizeof(p), "%s/home/neowall/config.vibe", base); touch(p);

    snprintf(d, sizeof(d), "%s/home", base);
    setenv("XDG_DATA_HOME", d, 1);
    snprintf(d, sizeof(d), "relative/dir:%s/a:%s/b", base, base);
    setenv("XDG_DATA_DIRS", d, 1);

    snprintf(p, sizeof(p), "%s/a/neowall/shaders/x.glsl", base);
    CHECK(neowall_find_data("shaders/x.glsl", out, sizeof(out)) && strcmp(out, p) == 0);
    snprintf(p, sizeof(p), "%s/b/neowall/shaders/only_b.glsl", base);
    CHECK(neowall_find_data("shaders/only_b.glsl", out, sizeof(out)) && strcmp(out, p) == 0);
    snprintf(p, sizeof(p), "%s/home/neowall/config.vibe", base);
    CHECK(neowall_find_data("config.vibe", out, sizeof(out)) && strcmp(out, p) == 0);
    CHECK(!neowall_find_data("shaders/missing.glsl", out, sizeof(out)) && out[0] == '\0');
    char tiny[8];
    CHECK(!neowall_find_data("shaders/x.glsl", tiny, sizeof(tiny)));

    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_DATA_DIRS");
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", base);
    CHECK(system(cmd) == 0);
}

int main(void) {
    test_lerp();
    test_clamp();
    test_ease();
    test_format_bytes();
    test_expand_path();
    test_parse_index();
    test_secure_runtime_dir();
    test_persisted_state_pruning();
    test_find_data();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
