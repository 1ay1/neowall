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
#include <time.h>

/* Prototypes from src/utils.c (kept in sync with include/neowall.h). */
float lerp(float a, float b, float t);
float clamp(float value, float min, float max);
float ease_in_out_cubic(float t);
void format_bytes(uint64_t bytes, char *buf, size_t size);
bool expand_path(const char *path, char *expanded, size_t size);
const char *neowall_secure_runtime_dir(void);

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

int main(void) {
    test_lerp();
    test_clamp();
    test_ease();
    test_format_bytes();
    test_expand_path();
    test_secure_runtime_dir();

    if (failures == 0) {
        printf("ok - %d checks passed\n", checks);
        return 0;
    }
    fprintf(stderr, "not ok - %d/%d checks failed\n", failures, checks);
    return 1;
}
