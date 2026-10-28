/* Golden trace of the adaptive resolution controller.
 *
 * Runs adaptive_update through every mode, quantized and continuous, against
 * eight load profiles (cheap, heavy, at-budget, spikes, ramp, sine, noise and
 * a one-off stall), 1500 frames each, and hashes the exact scale/target/stable
 * state after every frame. A refactor that changes any decision anywhere
 * changes the hash.
 *
 * If you change the controller on purpose, run with --print to see the trace,
 * check it does what you meant, and update GOLDEN below.
 */
#define _POSIX_C_SOURCE 200809L
#include <GL/gl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "neowall/shader/adaptive_scale.h"
#include "neowall/shader/reactive.h"

#define GOLDEN 0x98b573fcd6d2eda8ULL

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

typedef double (*profile_fn)(int frame);
static double p_cheap(int f) { (void)f; return 2.0; }
static double p_heavy(int f) { (void)f; return 30.0; }
static double p_edge(int f) { (void)f; return 15.5; }
static double p_spike(int f) { return (f / 120) % 2 ? 45.0 : 4.0; }
static double p_ramp(int f) { return 2.0 + f * 0.03; }
static double p_saw(int f) { return 8.0 + 10.0 * sin(f * 0.05); }
static double p_noisy(int f) { return 14.0 + ((f * 7919) % 13) - 6; }
static double p_gap(int f) { return f == 300 ? 900.0 : 12.0; }

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ULL;
    return h;
}

int main(int argc, char **argv) {
    bool print = argc > 1 && strcmp(argv[1], "--print") == 0;
    struct { const char *name; profile_fn fn; } profiles[] = {
        {"cheap", p_cheap}, {"heavy", p_heavy}, {"edge", p_edge}, {"spike", p_spike},
        {"ramp", p_ramp}, {"saw", p_saw}, {"noisy", p_noisy}, {"gap", p_gap},
    };
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int mode = ADAPTIVE_MODE_QUALITY; mode <= ADAPTIVE_MODE_BATTERY; mode++) {
        for (int quant = 0; quant <= 1; quant++) {
            for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); p++) {
                adaptive_state_t st;
                adaptive_config_t cfg = adaptive_config_for_mode((adaptive_mode_t)mode);
                cfg.use_quantized_levels = quant;
                adaptive_init(&st, &cfg);
                adaptive_set_enabled(&st, true);
                adaptive_set_target_fps(&st, 60.0f);
                adaptive_set_scale_range(&st, 0.25f, 1.0f);
                adaptive_init_gpu_timing(&st);
                double t = 1000.0;
                if (print) printf("== mode=%d quant=%d %s\n", mode, quant, profiles[p].name);
                for (int f = 0; f < 1500; f++) {
                    g_gpu_ms = profiles[p].fn(f);
                    adaptive_begin_frame(&st);
                    adaptive_end_frame(&st);
                    adaptive_update(&st, t);
                    t += (f == 300 && profiles[p].fn == p_gap) ? 2.0 : 1.0 / 60.0;
                    float s = adaptive_get_scale(&st), ts = adaptive_get_target_scale(&st);
                    unsigned char stable = adaptive_is_stable(&st);
                    h = fnv(h, &s, sizeof s);
                    h = fnv(h, &ts, sizeof ts);
                    h = fnv(h, &stable, 1);
                    if (print) printf("%a %a %d\n", (double)s, (double)ts, stable);
                }
                adaptive_destroy(&st);
            }
        }
    }
    if (print) return 0;
    printf("adaptive golden: %016llx (want %016llx)\n", (unsigned long long)h,
           (unsigned long long)GOLDEN);
    return h == GOLDEN ? 0 : 1;
}
