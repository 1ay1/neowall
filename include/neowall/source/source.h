/*
 * neowall/source/source.h — the open data plane
 * ============================================================================
 *
 * A `nw_source` is anything that can hand the GPU a live value: a float for a
 * uniform, or a texture for a sampler. CPU load is a source. The audio FFT is
 * a source. So is `exec("curl wttr.in", 300s)`, a file under /sys, a live
 * terminal, and a plain constant.
 *
 * This replaces two closed enums in the old engine — `uniform_bind_t` (24
 * arms) and `channel_source_t` (12 arms) — which between them defined every
 * live value a shader could ever see. Adding an input meant an enum arm, a
 * sampler in reactive.c, a case in manifest.c, and a release. Here a provider
 * registers a vtable and manifests can name it immediately.
 *
 * Two kinds, one interface:
 *
 *   NW_SOURCE_SCALAR   -> scalar()  feeds a `uniform float`
 *   NW_SOURCE_TEXTURE  -> texture() feeds a `sampler2D`
 *
 * Lifecycle:
 *
 *   spec ──parse──> nw_source_spec ──registry──> vtable->open() ──> nw_source
 *                                                                      │
 *                          nw_source_tick(s, ctx) every frame ─────────┤
 *                            (skipped while hidden or not yet due)     │
 *                          nw_source_scalar(s) / _texture(s)  ─────────┤
 *                          nw_source_destroy(s) ──> vtable->close() ───┘
 *
 * Visibility is the contract that protects neowall's reason to exist. The
 * engine pauses rendering when a wallpaper is covered; a data plane that kept
 * polling behind a maximized window would throw that away. So gating lives
 * HERE, in the core, not in each provider: nw_source_tick() is a no-op while
 * hidden, and providers holding real resources (threads, subprocesses, PTYs)
 * get on_visibility(false) so they can park. A provider cannot accidentally
 * burn CPU behind a window, because it does not decide when it runs.
 *
 * Time is injected via nw_source_ctx.now_ms and never read from a clock in
 * here, so scheduling is deterministic and testable with no GL, no display
 * server, and no sleeping in tests.
 */
#ifndef NEOWALL_SOURCE_H
#define NEOWALL_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "neowall/result.h"

/* Provider name, e.g. "cpu", "exec", "term". */
#define NW_SOURCE_NAME_MAX 32
/* Argument text inside name(...), e.g. the command line for exec(). */
#define NW_SOURCE_ARG_MAX 512
/* Cap on providers registered in one process. */
#define NW_SOURCE_REGISTRY_MAX 64
/* Provider-supplied failure text length (open() error buffer). */
#define NW_SOURCE_ERR_MAX 256

typedef enum {
    NW_SOURCE_SCALAR = 0, /* drives a `uniform float` */
    NW_SOURCE_TEXTURE     /* drives a `sampler2D` */
} nw_source_kind;

/*
 * A parsed reference to a source, before it is opened.
 *
 *   "cpu"                          -> name=cpu
 *   "0.85"                         -> name=const,  number=0.85, has_number
 *   "exec(\"uptime\", 5s)"         -> name=exec,   arg=uptime,  interval_ms=5000
 *   "image(\"~/a.png\", watch)"    -> name=image,  arg=~/a.png, flags=WATCH
 *
 * POD and copyable: no ownership, no allocation.
 */
typedef struct {
    char     name[NW_SOURCE_NAME_MAX];
    char     arg[NW_SOURCE_ARG_MAX];
    double   number;      /* literal value when has_number */
    bool     has_number;  /* spec was a bare number -> constant source */
    uint32_t interval_ms; /* 0 = provider default */
    uint32_t flags;       /* NW_SOURCE_SPEC_* */
} nw_source_spec;

#define NW_SOURCE_SPEC_WATCH 0x1u /* re-read on change rather than on a timer */

/* Per-tick context. Everything a provider may need about "now", passed in
 * rather than fetched, so ticking is pure with respect to the clock. */
typedef struct {
    uint64_t now_ms;      /* monotonic milliseconds */
    double   frame_dt;    /* seconds since previous frame */
    int      width;       /* target size in physical pixels */
    int      height;
    bool     visible;     /* is this output actually being seen */
} nw_source_ctx;

typedef struct nw_source_vtable {
    const char    *name;
    nw_source_kind kind;

    /* Poll interval when the spec does not override it. 0 = every frame. */
    uint32_t default_interval_ms;

    /* Create an instance. Return NULL and fill `err` on failure. `err` may be
     * NULL. A provider with no state may return a non-NULL sentinel. */
    void *(*open)(const nw_source_spec *spec, char *err, size_t err_cap);

    /* Release everything `open` acquired. May be NULL. */
    void (*close)(void *self);

    /* Refresh. Called only when visible and due. May be NULL for constants. */
    void (*tick)(void *self, const nw_source_ctx *ctx);

    /* Current value. Exactly one of these is used, per `kind`. `texture`
     * returns a GL texture name, or 0 when nothing is ready yet. */
    float (*scalar)(void *self);
    unsigned (*texture)(void *self);

    /* Visibility changed. Optional, for providers holding real resources:
     * park threads, stop subprocesses, release capture streams. */
    void (*on_visibility)(void *self, bool visible);
} nw_source_vtable;

/* An opened instance. Treat as opaque; use the accessors. */
typedef struct nw_source {
    const nw_source_vtable *vt;
    void                   *self;
    uint32_t                interval_ms;
    uint64_t                last_tick_ms;
    bool                    visible;
    bool                    ticked_once;
    float                   cached_scalar;
} nw_source;

/* ==========================================================================
 * Registry
 * ========================================================================== */

/* Register a provider. `vt` must outlive the process (static storage).
 * Fails on a duplicate name or a full registry. */
nw_result nw_source_register(const nw_source_vtable *vt);

/* Look up by provider name. NULL when unknown. */
const nw_source_vtable *nw_source_find(const char *name);

/* Register the built-in providers (const, exec, file, ...). Idempotent. */
void nw_source_register_builtins(void);

/*
 * exec() runs arbitrary commands, so it is opt-in. The engine enables it only
 * from an explicit user action (--allow-exec / config), never by default, so a
 * shader pulled off a gallery cannot quietly gain the ability to run code.
 * Off until this is called with true.
 */
void nw_source_exec_set_allowed(bool allowed);
bool nw_source_exec_allowed(void);

/* Drop every registration. Tests only. */
void nw_source_registry_reset(void);

/* Number of registered providers. */
size_t nw_source_registry_count(void);

/* Provider name by index, for diagnostics and `neowall sources`. */
const char *nw_source_registry_name_at(size_t index);

/* ==========================================================================
 * Spec parsing
 * ========================================================================== */

/*
 * Parse a manifest source reference into `out`.
 *
 * Accepted forms:
 *   bare word      cpu, audio_bass, term
 *   number         0.85, -1, 2e3          -> the `const` provider
 *   call           exec("uptime", 5s), image("~/a.png", watch)
 *
 * Call arguments: first a quoted string or bare token (the argument), then
 * optional comma-separated modifiers — a duration (`500ms`, `5s`, `2m`, `1h`)
 * sets the interval, `watch` sets NW_SOURCE_SPEC_WATCH.
 *
 * Returns NW_ERR_PARSE with context on malformed input. Does not check that
 * the provider exists; that is nw_source_open's job.
 */
nw_result nw_source_spec_parse(const char *text, nw_source_spec *out);

/* ==========================================================================
 * Instances
 * ========================================================================== */

/* Resolve `spec` against the registry and open it. On failure returns an error
 * and, when `err` is non-NULL, copies the provider's message into it. */
nw_result nw_source_open(const nw_source_spec *spec, nw_source *out, char *err, size_t err_cap);

/* Parse and open in one step. */
nw_result nw_source_open_text(const char *text, nw_source *out, char *err, size_t err_cap);

/* Close and zero. Safe on an already-destroyed or zeroed source. */
void nw_source_destroy(nw_source *src);

/*
 * Tick if visible and due. No-op otherwise. Returns true if the provider's
 * tick actually ran, which callers use to decide whether to re-upload.
 *
 * The first tick after becoming visible always runs, so a source that just
 * came back into view is fresh rather than showing a stale value until its
 * next interval elapses.
 */
bool nw_source_tick(nw_source *src, const nw_source_ctx *ctx);

/* Change visibility. Idempotent; forwards to on_visibility on a real edge. */
void nw_source_set_visible(nw_source *src, bool visible);

/* Latest scalar value. 0.0f for texture sources. */
float nw_source_scalar(const nw_source *src);

/* Latest GL texture name, or 0 when not ready. 0 for scalar sources. */
unsigned nw_source_texture(const nw_source *src);

/* Provider name, or "<none>" for a zeroed source. */
const char *nw_source_name(const nw_source *src);

/* Kind of the underlying provider. */
nw_source_kind nw_source_kind_of(const nw_source *src);

#endif /* NEOWALL_SOURCE_H */
