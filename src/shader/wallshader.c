/* See neowall/shader/wallshader.h. */
#include "neowall/shader/wallshader.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neowall/base.h"
#include "neowall/clock.h"
#include "neowall/log.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include "neowall/log.h"
#include "neowall/shader/reactive.h"
#include "neowall/shader/shader_source.h"
#include "neowall/shader/shader_state.h"
#include "neowall/source/source.h"
#include "neowall/shader/shader.h"

bool expand_path(const char *path, char *expanded, size_t size); /* utils.c */

/* A manifest `uniforms { uGlow audio_bass }` line, opened. */
typedef struct {
    const char *name;
    nw_source src;
    float value;
} binding;

struct wallshader {
    nw_shader_source *src;
    nw_engine *eng;
    NW_LIST(binding) binds;
    term_gpu *term;

    int frame;
    double last_present;
    float dt, fps;

    reactive_snapshot_t r;  /* eased toward the sampler each frame */
    bool r_primed;
    float r_widened[8];     /* the int/bool signals as floats */

    nw_shader_state_t state;
    float state_age;
    double state_checked_at;

    float win_count, focused[4];
    float windows[NW_SHADER_MAX_WINDOWS * 4];
    NW_LIST(nw_uniform_value) unis;
    NW_LIST(nw_uniform_value) extra;   /* the terminal's */
};

static nw_str g_errors;

const char *wallshader_last_errors(void) { return nw_str_cstr(&g_errors); }

static double mono_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void open_bindings(wallshader *s) {
    nw_source_register_builtins();
    nw_source_register_reactive();
    for (size_t i = 0; i < s->src->binds.len; i++) {
        const nw_uniform_bind *b = &s->src->binds.data[i];
        binding bd = {.name = b->name};
        char err[NW_SOURCE_ERR_MAX] = "";
        if (nw_is_err(nw_source_open_text(b->source, &bd.src, err, sizeof(err)))) {
            log_warn("%s: uniform %s: %s (reads 0)", s->src->manifest ? s->src->manifest : s->src->path,
                     b->name, err[0] ? err : b->source);
            continue;
        }
        if (!nw_list_push(&s->binds, bd)) nw_source_destroy(&bd.src);
    }
}

/* A compile that failed: keep the errors, and report it as the shader's
 * fault so a cycle skips to the next one. Frees src. */
static nw_result compile_failed(nw_shader_source *src, const char *errors) {
    nw_str_clear(&g_errors);
    nw_str_append(&g_errors, errors);
    log_error("%s failed to compile:\n%s", src->path ? src->path : "shader", nw_str_cstr(&g_errors));
    nw_shader_source_free(src);
    return nw_err(NW_ERR_PARSE, "shader failed to compile");
}

/* A wallshader around a compiled engine. Takes both. */
static nw_result wrap(nw_shader_source *src, nw_engine *eng, wallshader **out) {
    wallshader *s = calloc(1, sizeof(*s));
    if (!s) {
        nw_engine_free(eng);
        nw_shader_source_free(src);
        return nw_err(NW_ERR_OOM, "wallshader");
    }
    s->src = src;
    s->eng = eng;
    if (src->state_pass && src->path) nw_shader_state_load(src->path, &s->state);
    open_bindings(s);
    nw_str_clear(&g_errors);
    *out = s;
    return nw_ok();
}

static nw_result finish_load(nw_shader_source *src, wallshader **out) {
    nw_engine *eng = NULL;
    nw_result r = nw_engine_new(src, &eng);
    if (nw_is_err(r)) {
        if (r.status != NW_ERR_GL) {
            nw_shader_source_free(src);
            return r;
        }
        return compile_failed(src, nw_engine_last_errors());
    }
    return wrap(src, eng, out);
}

/* Find and parse a shader by path or bare name. */
static nw_result parse_path(const char *path, nw_shader_source **out) {
    nw_str_clear(&g_errors);
    /* a bare name is found in the shader dirs; ~ is the home dir */
    char found[4096], full[4096];
    if (!shader_resolve_path(path, found, sizeof(found))) {
        nw_str_printf(&g_errors, "%s: no such shader\n", path);
        return nw_err(NW_ERR_IO, "shader not found");
    }
    if (!expand_path(found, full, sizeof(full))) snprintf(full, sizeof(full), "%s", found);
    nw_shader_source *src = NULL;
    nw_result r = nw_shader_source_load(full, &src);
    if (nw_is_err(r)) {
        nw_str_printf(&g_errors, "%s: %s\n", path, r.context ? r.context : "can't load");
        /* anything wrong with the files is the shader's problem, not the
         * output's: callers cycling a list skip to the next one */
        if (r.status == NW_ERR_NOT_FOUND) return nw_err(NW_ERR_IO, r.context);
        if (r.status == NW_ERR_UNSUPPORTED) return nw_err(NW_ERR_PARSE, r.context);
        return r;
    }
    *out = src;
    return nw_ok();
}

nw_result wallshader_load(const char *path, wallshader **out) {
    nw_shader_source *src = NULL;
    NW_TRY(parse_path(path, &src));
    return finish_load(src, out);
}

struct wallshader_pending {
    nw_shader_source *src; /* the build borrows it; ours until wrap() */
    nw_engine_build *b;
};

nw_result wallshader_load_start(const char *path, wallshader_pending **out) {
    wallshader_pending *p = calloc(1, sizeof(*p));
    if (!p) return nw_err(NW_ERR_OOM, "wallshader");
    nw_result r = parse_path(path, &p->src);
    if (nw_is_ok(r)) r = nw_engine_build_start(p->src, &p->b);
    if (nw_is_err(r)) {
        wallshader_load_cancel(p);
        return r;
    }
    *out = p;
    return nw_ok();
}

nw_build_state wallshader_load_poll(wallshader_pending *p, wallshader **out) {
    if (!p->b) return NW_BUILD_FAILED; /* already reported */
    nw_build_state st = nw_engine_build_poll(p->b);
    if (st == NW_BUILD_PENDING) return st;
    nw_shader_source *src = p->src;
    p->src = NULL;
    if (st == NW_BUILD_FAILED) {
        (void)compile_failed(src, nw_engine_build_errors(p->b));
    } else if (nw_is_err(wrap(src, nw_engine_build_take(p->b), out))) {
        st = NW_BUILD_FAILED;
    }
    nw_engine_build_free(p->b);
    p->b = NULL;
    return st;
}

void wallshader_load_cancel(wallshader_pending *p) {
    if (!p) return;
    nw_engine_build_free(p->b);
    nw_shader_source_free(p->src);
    free(p);
}

nw_result wallshader_load_text(const char *text, const char *name, wallshader **out) {
    nw_str_clear(&g_errors);
    nw_shader_source *src = NULL;
    nw_result r = nw_shader_source_parse(text, name, NULL, &src);
    if (nw_is_err(r)) {
        nw_str_printf(&g_errors, "%s: %s\n", name ? name : "shader", r.context ? r.context : "can't parse");
        return r;
    }
    return finish_load(src, out);
}

/* Read back the state pass and persist it, at most once a second (it
 * stalls the GPU). A blown-up simulation (NaN) is never saved. */
static void save_state(wallshader *s, double now) {
    if (!s->src->state_pass || !s->src->path || now - s->state_checked_at < 1.0) return;
    s->state_checked_at = now;
    float v[NW_STATE_FLOATS];
    if (!nw_engine_read_state(s->eng, v)) return;
    for (int i = 0; i < NW_STATE_FLOATS; i++)
        if (!isfinite(v[i])) return;
    if (!memcmp(v, s->state.values, sizeof(v))) return;
    memcpy(s->state.values, v, sizeof(v));
    if (!nw_shader_state_save(s->src->path, &s->state)) log_warn("couldn't save state for %s", s->src->path);
}

void wallshader_free(wallshader *s) {
    if (!s) return;
    if (s->eng) save_state(s, s->state_checked_at + 1.0);
    for (size_t i = 0; i < s->binds.len; i++) nw_source_destroy(&s->binds.data[i].src);
    nw_list_free(&s->binds);
    nw_list_free(&s->unis);
    nw_list_free(&s->extra);
    term_gpu_free(s->term);
    nw_engine_free(s->eng);
    nw_shader_source_free(s->src);
    free(s);
}

void wallshader_attach_terminal(wallshader *s, term_gpu *t) {
    term_gpu_free(s->term);
    s->term = t;
}

term_gpu *wallshader_terminal(wallshader *s) { return s ? s->term : NULL; }

bool wallshader_animated(const wallshader *s) { return s->term || nw_engine_animated(s->eng); }

void wallshader_set_visible(wallshader *s, bool visible) {
    if (!s) return;
    for (size_t i = 0; i < s->binds.len; i++) nw_source_set_visible(&s->binds.data[i].src, visible);
}
int wallshader_pass_count(const wallshader *s) { return (int)s->src->passes.len; }

/* One snapshot per frame, eased over ~250ms so a 4Hz sampler doesn't step. */
static void sample_reactive(wallshader *s) {
    reactive_snapshot_t prev = s->r;
    reactive_get(&s->r);
    if (!s->r_primed) {
        s->r_primed = true;
        return;
    }
    reactive_snapshot_t *r = &s->r;
    float k = 1.0f - expf(-s->dt / 0.25f);
#define EASE(f) r->f = prev.f + (r->f - prev.f) * k
    EASE(cpu); EASE(cpu_max); EASE(cpu_spread); EASE(ram); EASE(swap);
    EASE(net_down); EASE(net_up); EASE(disk_read); EASE(disk_write); EASE(load_avg);
    EASE(gpu); EASE(nv_gpu); EASE(nv_power);
    EASE(thermal); EASE(activity); EASE(procs);
    EASE(key_energy); EASE(mouse_energy);
    for (int i = 0; i < r->cpu_cores && i < REACTIVE_MAX_CPU_CORES; i++) EASE(cpu_per[i]);
#undef EASE
}

static void push(wallshader *s, const char *name, int count, const float *v) {
    (void)nw_list_push(&s->unis, ((nw_uniform_value){.name = name, .count = count, .v = v}));
}

static void push_int(wallshader *s, const char *name, const float *v) {
    (void)nw_list_push(&s->unis, ((nw_uniform_value){.name = name, .count = 1, .v = v, .is_int = true}));
}

/* The reactive uniforms the stdlib declares (shader_stdlib.h). */
static void push_reactive(wallshader *s) {
    const reactive_snapshot_t *r = &s->r;
    float *w = s->r_widened;
    w[0] = (float)r->cpu_cores;
    w[1] = (float)r->proc_count;
    w[2] = r->nv_active ? 1.0f : 0.0f;
    w[3] = r->charging ? 1.0f : 0.0f;
    w[4] = r->audio_active ? 1.0f : 0.0f;
    static const struct {
        const char *name;
        size_t off;
    } f[] = {
#define F(n, field) {n, offsetof(reactive_snapshot_t, field)}
        F("iCpu", cpu), F("iCpuMax", cpu_max), F("iCpuSpread", cpu_spread), F("iRam", ram),
        F("iRamGB", ram_gb), F("iRamTotalGB", ram_total_gb), F("iSwap", swap), F("iNetDown", net_down),
        F("iNetUp", net_up), F("iNetDownRaw", net_down_mbs), F("iNetUpRaw", net_up_mbs),
        F("iDiskRead", disk_read), F("iDiskWrite", disk_write), F("iLoad", load_avg),
        F("iLoadRaw", load_raw), F("iCpuTemp", cpu_temp), F("iCpuTempC", cpu_temp_c), F("iGpu", gpu),
        F("iGpuTemp", gpu_temp), F("iGpuTempC", gpu_temp_c), F("iNvGpu", nv_gpu), F("iNvVram", nv_vram),
        F("iNvGpuTempC", nv_temp_c), F("iNvPower", nv_power), F("iThermal", thermal),
        F("iActivity", activity), F("iPulse", pulse), F("iUptimeHours", uptime_hours), F("iProcs", procs),
        F("iBattery", battery), F("iTimeOfDay", time_of_day), F("iSun", sun),
        F("iDayFraction", day_fraction), F("iKeyEnergy", key_energy), F("iMouseEnergy", mouse_energy),
        F("iAudioLevel", audio_level), F("iAudioBass", audio_bass), F("iAudioMid", audio_mid),
        F("iAudioTreble", audio_treble), F("iAudioBeat", audio_beat),
#undef F
    };
    for (size_t i = 0; i < NW_COUNTOF(f); i++) push(s, f[i].name, 1, (const float *)((const char *)r + f[i].off));
    (void)nw_list_push(&s->unis, ((nw_uniform_value){.name = "iCpuCores", .count = REACTIVE_MAX_CPU_CORES,
                                                     .v = r->cpu_per, .float_array = true}));
    push_int(s, "iCpuCoreCount", &w[0]);
    push_int(s, "iProcCount", &w[1]);
    push(s, "iNvActive", 1, &w[2]);
    push(s, "iCharging", 1, &w[3]);
    push(s, "iAudioActive", 1, &w[4]);
}

void wallshader_draw(wallshader *s, const wallshader_frame *f) {
    double now = mono_now();
    /* iTimeDelta: the gap between the times the two frames are shown when
     * we know them, else wall time; clamped so a long pause can't blow up a
     * physics shader and 1/iTimeDelta stays finite */
    double stamp = f->present_at > 0 ? f->present_at : now;
    if (s->frame > 0) {
        float dt = (float)(stamp - s->last_present);
        if (dt > 0.25f) dt = 0.25f;
        if (dt < 0.0001f) dt = 0.0001f;
        s->dt = dt;
        float inst = 1.0f / dt;
        s->fps = s->fps > 0 ? s->fps * 0.9f + inst * 0.1f : inst;
    } else {
        s->dt = 1.0f / 60.0f;
        s->fps = 60.0f;
    }
    s->last_present = stamp;

    sample_reactive(s);
    s->unis.len = 0;
    push_reactive(s);

    nw_source_ctx ctx = {.now_ms = (uint64_t)(now * 1000.0), .frame_dt = s->dt, .width = f->width,
                         .height = f->height, .visible = true};
    for (size_t i = 0; i < s->binds.len; i++) {
        binding *b = &s->binds.data[i];
        (void)nw_source_tick(&b->src, &ctx);
        b->value = nw_source_scalar(&b->src);
        push(s, b->name, 1, &b->value);
    }

    int nwin = f->windows ? f->window_count : 0;
    if (nwin > NW_SHADER_MAX_WINDOWS) nwin = NW_SHADER_MAX_WINDOWS;
    if (nwin > 0) memcpy(s->windows, f->windows, (size_t)nwin * 4 * sizeof(float));
    s->win_count = (float)nwin;
    if (f->focused) memcpy(s->focused, f->focused, sizeof(s->focused));
    else memset(s->focused, 0, sizeof(s->focused));
    /* always set the count: a loop to iWindowCount must see 0, not last frame's */
    push_int(s, "iWindowCount", &s->win_count);
    if (nwin > 0) push(s, "iWindows", nwin * 4, s->windows);
    push(s, "iFocusedWindow", 4, s->focused);

    s->state_age = nw_shader_state_age(&s->state);
    push(s, "iState", NW_STATE_FLOATS, s->state.values);
    push(s, "iStateAge", 1, &s->state_age);

    const nw_uniform_value *tu = NULL;
    const nw_texture_value *tt = NULL;
    size_t ntu = 0, ntt = 0;
    if (s->term) term_gpu_frame(s->term, f->time, &tu, &ntu, &tt, &ntt);

    float date[4] = {0};
    time_t t = nw_clock_now();
    struct tm tm;
    if (localtime_r(&t, &tm)) {
        date[0] = (float)(tm.tm_year + 1900);
        date[1] = (float)(tm.tm_mon + 1);
        date[2] = (float)tm.tm_mday;
        date[3] = (float)(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
    }

    nw_frame_in in = {
        .width = f->width,
        .height = f->height,
        .time = f->time,
        .dt = s->dt,
        .fps = s->fps,
        .frame = s->frame,
        .mouse = {f->mouse[0], f->mouse[1], 0, 0},
        .date = {date[0], date[1], date[2], date[3]},
        .span_offset = {(float)f->span_x, (float)f->span_y},
        .span_w = f->span_w,
        .span_h = f->span_h,
        .uniforms = s->unis.data,
        .uniform_count = s->unis.len,
        .uniforms2 = tu,
        .uniform2_count = ntu,
        .audio_spectrum = s->r.audio_spectrum,
        .audio_waveform = s->r.audio_waveform,
        .textures = tt,
        .texture_count = ntt,
    };
    for (int c = 0; c < 4; c++) {
        in.channel_override[c] = f->channels[c];
        in.channel_override_size[c][0] = (float)f->channel_w[c];
        in.channel_override_size[c][1] = (float)f->channel_h[c];
    }

    /* the context is shared with transitions and overlays: don't trust
     * whatever state they left */
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    nw_engine_frame(s->eng, &in);
    s->frame++;
    save_state(s, now);
}
