/* The adaptive resolution controller, headless.
 *
 * Drives adaptive_update() with a simulated 60 Hz frame clock and a GPU cost
 * we choose, through stubbed timer queries. Pins the regression where every
 * shader, however cheap, was walked down to ~50% resolution in its first
 * second: the controller decided off the target frame time adaptive_init
 * seeds (16.67 ms against a 14.7 ms budget = "14% over") before any real
 * frame had been measured. On garden that meant ~100 buffer reallocations and
 * 250 ms stalls at startup. */

#define _POSIX_C_SOURCE 200809L
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>

#include "neowall/shader/adaptive_scale.h"
#include "neowall/shader/reactive.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* ── stubs: the GPU's answer is whatever this frame costs ─────────────── */

static double g_gpu_ms = 2.0;

void glGenQueries(GLsizei n, GLuint *ids) { for (GLsizei i = 0; i < n; i++) ids[i] = (GLuint)(i + 1); }
void glDeleteQueries(GLsizei n, const GLuint *ids) { (void)n; (void)ids; }
void glBeginQuery(GLenum t, GLuint id) { (void)t; (void)id; }
void glEndQuery(GLenum t) { (void)t; }
void glGetQueryObjectiv(GLuint id, GLenum p, GLint *out) { (void)id; (void)p; *out = 1; }
void glGetQueryObjectui64v(GLuint id, GLenum p, GLuint64 *out) {
    (void)id; (void)p;
    *out = (GLuint64)(g_gpu_ms * 1e6);
}
void reactive_get(reactive_snapshot_t *out) { memset(out, 0, sizeof(*out)); }
void log_info(const char *fmt, ...) { (void)fmt; }

/* Run `frames` frames at 60 Hz costing `gpu_ms` each. Returns the lowest
 * scale seen and counts scale changes. */
static float run(adaptive_state_t *st, double *t, int frames, double gpu_ms, int *changes) {
    g_gpu_ms = gpu_ms;
    float lowest = adaptive_get_scale(st);
    float last = lowest;
    for (int i = 0; i < frames; i++) {
        adaptive_begin_frame(st);
        adaptive_end_frame(st);
        adaptive_update(st, *t);
        *t += 1.0 / 60.0;
        float s = adaptive_get_scale(st);
        if (s < lowest) lowest = s;
        if (changes && s != last) (*changes)++;
        last = s;
    }
    return lowest;
}

static void fresh(adaptive_state_t *st) {
    adaptive_init(st, NULL);
    adaptive_set_enabled(st, true);
    adaptive_set_target_fps(st, 60.0f);
    adaptive_set_scale_range(st, 0.25f, 1.0f);
    adaptive_init_gpu_timing(st);
}

int main(void) {
    adaptive_state_t st;
    double t = 1000.0;

    /* 1. A cheap shader (2 ms of a 16.7 ms frame) stays at full resolution
     * from the very first frame. Before the fix it fell to ~0.53 within a
     * second and climbed back over the next few, 65 scale changes later. */
    fresh(&st);
    int changes = 0;
    float lowest = run(&st, &t, 600, 2.0, &changes);
    CHECK(lowest >= 0.99f, "cheap shader never drops resolution (lowest %.2f)", lowest);
    CHECK(changes == 0, "cheap shader causes no scale changes (got %d)", changes);

    /* 2. A shader that genuinely can't keep up (30 ms of a 16.7 ms frame)
     * still gets scaled down: the fix only stops decisions on invented
     * numbers, not on real ones. */
    fresh(&st);
    lowest = run(&st, &t, 600, 30.0, NULL);
    CHECK(lowest < 0.8f, "an over-budget shader is scaled down (lowest %.2f)", lowest);

    /* 3. And when the load goes away again, it comes back up. */
    float before = adaptive_get_scale(&st);
    run(&st, &t, 1200, 2.0, NULL);
    CHECK(adaptive_get_scale(&st) > before,
          "load gone: scale recovers (%.2f -> %.2f)", before, adaptive_get_scale(&st));

    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
