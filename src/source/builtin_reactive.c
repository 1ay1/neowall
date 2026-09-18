/*
 * builtin_reactive.c — the live machine signals, as data-plane providers.
 *
 * This replaces `uniform_bind_t`: 24 enum arms, a name->enum table, and a
 * 24-case switch copying fields out of reactive_snapshot_t, spread across
 * shader_multipass.c and manifest.c. All of it is one table here.
 *
 * Two things fall out of the port.
 *
 * First, the enum only ever exposed 24 of the ~40 signals reactive.c already
 * samples. Absolute values (ram_gb, net_down_mbs, load_raw, cpu_temp_c), the
 * fused shaping signals (thermal, activity, pulse), the NVIDIA block, and
 * per-core spread were all being computed every frame and then thrown away
 * unless a shader happened to use the one builtin uniform they were wired to.
 * A table costs one line per signal, so they are all bindable now.
 *
 * Second, sampling is shared. reactive_snapshot_t is ~4KB (two 512-float audio
 * rows), so copying it per bound uniform per frame would be silly. Every
 * instance reads one process-wide snapshot refreshed at most once per frame,
 * keyed on the injected timestamp — so twenty bound uniforms cost exactly one
 * reactive_get(), same as one.
 */

#include "neowall/source/source.h"
#include "neowall/shader/reactive.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ==========================================================================
 * Shared per-frame snapshot
 *
 * reactive_sample() is already self-throttled and called from the main loop;
 * this is only about not copying 4KB once per bound uniform.
 * ========================================================================== */

static reactive_snapshot_t g_snapshot;
static uint64_t            g_snapshot_ms;
static bool                g_snapshot_valid;

static const reactive_snapshot_t *reactive_frame(uint64_t now_ms) {
    if (!g_snapshot_valid || now_ms != g_snapshot_ms) {
        reactive_get(&g_snapshot);
        g_snapshot_ms    = now_ms;
        g_snapshot_valid = true;
    }
    return &g_snapshot;
}

/* Tests and reload paths need to drop the cache so a stale frame is not
 * served after the underlying sampler is reinitialised. */
void nw_reactive_invalidate(void) { g_snapshot_valid = false; }

/* ==========================================================================
 * The signal table
 *
 * `offset` is into reactive_snapshot_t. The kind tag exists because a few
 * signals are int or bool rather than float; the GPU only takes floats, so
 * they are widened on read.
 * ========================================================================== */

typedef enum { SIG_FLOAT, SIG_INT, SIG_BOOL } signal_kind;

typedef struct {
    const char *name;
    const char *alias; /* legacy spelling from the old enum table, or NULL */
    size_t      offset;
    signal_kind kind;
} reactive_signal;

#define SIG(field, kind_) offsetof(reactive_snapshot_t, field), kind_

static const reactive_signal g_signals[] = {
    /* load */
    {"cpu",          NULL,          SIG(cpu, SIG_FLOAT)},
    {"cpu_max",      NULL,          SIG(cpu_max, SIG_FLOAT)},
    {"cpu_spread",   NULL,          SIG(cpu_spread, SIG_FLOAT)},
    {"cpu_cores",    NULL,          SIG(cpu_cores, SIG_INT)},
    {"ram",          NULL,          SIG(ram, SIG_FLOAT)},
    {"ram_gb",       NULL,          SIG(ram_gb, SIG_FLOAT)},
    {"ram_total_gb", NULL,          SIG(ram_total_gb, SIG_FLOAT)},
    {"swap",         NULL,          SIG(swap, SIG_FLOAT)},
    {"net_down",     "netdown",     SIG(net_down, SIG_FLOAT)},
    {"net_up",       "netup",       SIG(net_up, SIG_FLOAT)},
    {"net_down_mbs", NULL,          SIG(net_down_mbs, SIG_FLOAT)},
    {"net_up_mbs",   NULL,          SIG(net_up_mbs, SIG_FLOAT)},
    {"disk_read",    "diskread",    SIG(disk_read, SIG_FLOAT)},
    {"disk_write",   "diskwrite",   SIG(disk_write, SIG_FLOAT)},
    {"load",         NULL,          SIG(load_avg, SIG_FLOAT)},
    {"load_raw",     NULL,          SIG(load_raw, SIG_FLOAT)},

    /* thermals + GPU */
    {"cpu_temp",     "cputemp",     SIG(cpu_temp, SIG_FLOAT)},
    {"cpu_temp_c",   NULL,          SIG(cpu_temp_c, SIG_FLOAT)},
    {"gpu",          NULL,          SIG(gpu, SIG_FLOAT)},
    {"gpu_temp",     "gputemp",     SIG(gpu_temp, SIG_FLOAT)},
    {"gpu_temp_c",   NULL,          SIG(gpu_temp_c, SIG_FLOAT)},
    {"nv_gpu",       NULL,          SIG(nv_gpu, SIG_FLOAT)},
    {"nv_vram",      NULL,          SIG(nv_vram, SIG_FLOAT)},
    {"nv_temp_c",    NULL,          SIG(nv_temp_c, SIG_FLOAT)},
    {"nv_power",     NULL,          SIG(nv_power, SIG_FLOAT)},
    {"nv_active",    NULL,          SIG(nv_active, SIG_BOOL)},

    /* fused shaping signals */
    {"thermal",      NULL,          SIG(thermal, SIG_FLOAT)},
    {"activity",     NULL,          SIG(activity, SIG_FLOAT)},
    {"pulse",        NULL,          SIG(pulse, SIG_FLOAT)},

    /* uptime + processes */
    {"uptime",       NULL,          SIG(uptime_hours, SIG_FLOAT)},
    {"procs",        "processes",   SIG(procs, SIG_FLOAT)},
    {"proc_count",   NULL,          SIG(proc_count, SIG_INT)},

    /* power */
    {"battery",      NULL,          SIG(battery, SIG_FLOAT)},
    {"charging",     NULL,          SIG(charging, SIG_BOOL)},

    /* time */
    {"time_of_day",  "timeofday",   SIG(time_of_day, SIG_FLOAT)},
    {"sun",          NULL,          SIG(sun, SIG_FLOAT)},
    {"day_fraction", NULL,          SIG(day_fraction, SIG_FLOAT)},

    /* input energy */
    {"key_energy",   "keys",        SIG(key_energy, SIG_FLOAT)},
    {"mouse_energy", "mouse",       SIG(mouse_energy, SIG_FLOAT)},

    /* audio */
    {"audio_level",  "audio",       SIG(audio_level, SIG_FLOAT)},
    {"audio_bass",   "bass",        SIG(audio_bass, SIG_FLOAT)},
    {"audio_mid",    "mid",         SIG(audio_mid, SIG_FLOAT)},
    {"audio_treble", "treble",      SIG(audio_treble, SIG_FLOAT)},
    {"audio_beat",   "beat",        SIG(audio_beat, SIG_FLOAT)},
    {"audio_active", NULL,          SIG(audio_active, SIG_BOOL)},
};

#undef SIG

#define SIGNAL_COUNT (sizeof(g_signals) / sizeof(g_signals[0]))

static float signal_read(const reactive_signal *sig, const reactive_snapshot_t *snap) {
    const char *base = (const char *)snap;
    switch (sig->kind) {
        case SIG_INT:
            return (float)(*(const int *)(const void *)(base + sig->offset));
        case SIG_BOOL:
            return *(const bool *)(const void *)(base + sig->offset) ? 1.0f : 0.0f;
        case SIG_FLOAT:
        default:
            return *(const float *)(const void *)(base + sig->offset);
    }
}

/* ==========================================================================
 * Providers
 *
 * Each signal registers under its own name, so a manifest says `cpu` rather
 * than `reactive("cpu")`. Every existing manifest keeps working unchanged.
 * ========================================================================== */

typedef struct {
    const reactive_signal *sig;
    float                  value;
} reactive_state;

/* The vtable name is the provider name, which is how open() knows which signal
 * it is being asked for — one open() shared by all ~44 registrations. */
static void *reactive_open(const nw_source_spec *spec, char *err, size_t err_cap) {
    const reactive_signal *found = NULL;
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        if (strcasecmp(g_signals[i].name, spec->name) == 0 ||
            (g_signals[i].alias && strcasecmp(g_signals[i].alias, spec->name) == 0)) {
            found = &g_signals[i];
            break;
        }
    }
    if (!found) {
        snprintf(err, err_cap, "no such reactive signal");
        return NULL;
    }
    reactive_state *st = calloc(1, sizeof(*st));
    if (!st) {
        snprintf(err, err_cap, "out of memory");
        return NULL;
    }
    st->sig = found;
    return st;
}

static void reactive_close(void *self) { free(self); }

static void reactive_tick(void *self, const nw_source_ctx *ctx) {
    reactive_state *st = self;
    st->value          = signal_read(st->sig, reactive_frame(ctx->now_ms));
}

static float reactive_scalar(void *self) { return ((reactive_state *)self)->value; }

/* One vtable per signal. They differ only in `name`; the callbacks are shared.
 * Built once at registration because nw_source_register borrows the pointer. */
static nw_source_vtable g_vtables[SIGNAL_COUNT * 2];

void nw_source_register_reactive(void) {
    /* Idempotence comes from the registry rejecting duplicates, not from a
     * static flag. A flag would be a second source of truth that disagrees
     * with the registry the moment it is reset, leaving nothing registered and
     * every uniform silently reading 0. */
    size_t n = 0;
    for (size_t i = 0; i < SIGNAL_COUNT; i++) {
        /* Reactive signals are sampled on the main loop at ~4Hz; ticking every
         * frame just reads the shared snapshot, which is what we want so a
         * uniform never lags its source. */
        g_vtables[n] = (nw_source_vtable){
            .name   = g_signals[i].name,
            .kind   = NW_SOURCE_SCALAR,
            .open   = reactive_open,
            .close  = reactive_close,
            .tick   = reactive_tick,
            .scalar = reactive_scalar,
        };
        nw_source_register(&g_vtables[n]);
        n++;

        if (g_signals[i].alias) {
            g_vtables[n]      = g_vtables[n - 1];
            g_vtables[n].name = g_signals[i].alias;
            nw_source_register(&g_vtables[n]);
            n++;
        }
    }
}

size_t nw_source_reactive_signal_count(void) { return SIGNAL_COUNT; }
