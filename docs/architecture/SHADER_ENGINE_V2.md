# Shader engine v2 — a render graph with pluggable data

Status: the old engine is gone. `src/shader/` now runs the rewrite's shader
system (`source.c`, `assemble.c`, `engine.c`, `wallshader.c`, `term_gpu.c`;
see docs/ARCHITECTURE.md §8). It already has what §1 asked for: any number of
passes in dependency order, passes named rather than enumerated, manifest
uniforms bound through `nw_source` providers, and a GL-free source model
tested without a display. The engine orders passes itself, so the separate
`graph/` layer this plan built was deleted unused. What's left is a
frontend vtable (§4) and layers (slice 7).

The sections below are the original design, kept for the reasoning.

---

## 1. Why

The engine works, but the design has one bad assumption baked all the way
through it: **a shader is a Shadertoy shader**. That shows up as

- `MULTIPASS_MAX_PASSES 5` and `PASS_TYPE_BUFFER_A..D` — the pass list is
  Shadertoy's pass list, as an enum, in the core type.
- `MULTIPASS_MAX_CHANNELS 4` — because `iChannel0..3`.
- `mainImage(out vec4, in vec2)` assumed by the wrapper that every pass gets.
- `uniform_bind_t` — 24 enum arms, one per live scalar the GPU can see.
- `channel_source_t` — 12 enum arms, one per live texture the GPU can see.

So every new input needs an enum arm, a sampler in `reactive.c`, a case in
`manifest.c`, and a release. Weather, MPRIS, album art, fan RPM, unread mail —
all of them are a patch to the core. That is the actual ceiling on the project,
not GPU time.

And `multipass_shader_t` owns everything at once: parse output, GL objects,
optimizer counters, the terminal PTY, reactive samples, the virtual clock,
adaptive scale state. 3034 lines in one file, 82 call sites on the struct.
Nothing in there can be tested without a GL context.

## 2. The shape

Six layers, each depending only on the ones above it. No layer knows about
Shadertoy except the frontend that is named after it.

```
  gfx/        GL objects. programs, textures, targets. zero shader semantics.
  graph/      the render graph. passes, edges, resources, scheduling.
  source/     the data plane. open registry of live scalar + texture providers.
  glsl/       GLSL text assembly. prelude, stdlib, namespace shadowing.
  frontend/   parsers that BUILD graphs. shadertoy is one of these.
  engine/     policy. adaptive scale, pass throttling, damage, hot reload.
```

The rule that makes it work: **the core takes a graph and runs it.** It does
not know where the graph came from. Shadertoy is a frontend that reads a
`.glsl` and emits a graph. A native manifest is a frontend that reads a
`.neowall` and emits a graph. A layered desktop canvas is a frontend that emits
a bigger graph. Adding a format is a new file in `frontend/`, not a change to
the core.

## 3. Core types

```c
/* topology — immutable once built */
typedef struct {
    nw_pass_vec  passes;      /* N, not 5 */
    nw_res_vec   resources;   /* render targets, dynamically sized */
    int         *order;       /* topological execution order */
    int          output;      /* index of the pass that hits the screen */
} nw_graph;

typedef struct {
    char          name[NW_PASS_NAME_MAX];
    char         *source;     /* assembled GLSL */
    nw_bind_vec   bindings;   /* M, not 4 */
    nw_rect       viewport;   /* fractional; whole screen by default */
    nw_blend      blend;
    uint32_t      flags;      /* mipmaps, feedback, srgb, ... */
} nw_pass;

typedef struct {
    int         slot;         /* sampler unit */
    nw_source  *source;       /* ANY provider. see below. */
} nw_binding;
```

Two things that matter here:

- `passes` and `bindings` are `nw_vec`, not fixed arrays. A Shadertoy shader
  emits 5 and 4. A dashboard emits 20 and 9. Same code path.
- A binding points at a `nw_source`, an interface. "Buffer A" is not special —
  it is a provider that happens to be backed by another pass in this graph, and
  it sits next to `term("btop")` and `image("~/art.png")` in the same list.

State lives apart from topology:

```c
nw_graph        g;   /* what reads what.        built once, or on reload */
nw_graph_state  s;   /* GL objects + live data. rebuilt on resize */
```

That split is what makes hot reload cheap — build the new graph, diff it
against the old, keep every resource whose shape did not change.

## 4. Shadertoy becomes a plugin

A frontend is a two-method vtable:

```c
typedef struct {
    const char *name;
    bool (*probe)(const char *path, const char *text);
    nw_result (*build)(const char *path, const char *text, nw_graph *out);
} nw_frontend;
```

`src/frontend/shadertoy.c` — probes for `mainImage`, splits `Buffer A..D`
markers, emits up to 5 passes with the feedback edges wired and the
`mainImage` wrapper applied to each. That is the entire Shadertoy footprint of
the project. Roughly 400 lines, one file, deletable.

Everything that is currently a Shadertoy concept in the core becomes a graph
concept:

| Shadertoy thing | graph thing |
|---|---|
| Buffer A..D | passes with a feedback (ping-pong) resource |
| Image pass | `graph.output` |
| `iChannel0..3` | bindings 0..3, no upper bound |
| `mainImage` wrapper | a prelude/epilogue the frontend chooses |
| `iTime`, `iDate`, ... | builtin scalar sources, same as any other |

## 5. The open data plane

This is the unlock and it is slice 1, already landed. See
`include/neowall/source/source.h`.

Both enums are gone. A source is a vtable:

```c
typedef struct nw_source_vtable {
    const char      *name;
    nw_source_kind   kind;                 /* SCALAR or TEXTURE */
    uint32_t         default_interval_ms;  /* 0 = every frame */
    void     *(*open)(const nw_source_spec *, char *err, size_t cap);
    void      (*close)(void *self);
    void      (*tick)(void *self, const nw_source_ctx *);
    float     (*scalar)(void *self);
    unsigned  (*texture)(void *self);
    void      (*on_visibility)(void *self, bool visible);
} nw_source_vtable;
```

Providers register at startup. Manifests name them:

```
uniforms {
  uLoad    cpu                                  # builtin
  uRain    exec("curl -s wttr.in/?format=%p", 300s)
  uFan     file("/sys/class/hwmon/hwmon2/fan1_input", 2s)
  uExp     0.85                                 # a constant is a provider too
}

channel0 term("btop")                           # a live PTY as a texture
channel1 image("~/.cache/album-art.png", watch)
```

`exec` and `file` mean the Inputs branch stops being a list we maintain.
Weather, MPRIS, notifications, CI status, stock tickers — config lines, not
patches. `term()` as a texture is the one nobody else can do: the PTY→GPU cell
grid already exists for terminal wallpapers, it just was not exposed as a
binding.

Time is injected (`ctx->now_ms`), never read from a clock inside the layer, so
the whole data plane is deterministic and unit-testable with no GL, no display,
and no sleeping in tests.

### Reactive signals (slice 2)

`src/source/builtin_reactive.c` replaces `uniform_bind_t` with a table: one
line per signal, `offsetof` into `reactive_snapshot_t`. Two things came out of
the port.

The enum exposed 24 signals; `reactive.c` was already sampling about 44. The
absolute values (`ram_gb`, `net_down_mbs`, `cpu_temp_c`, `load_raw`), the fused
shaping signals (`thermal`, `activity`, `pulse`), the NVIDIA block, and
per-core spread were all computed every frame and thrown away. A table costs a
line each, so they are all bindable now. Every legacy name and alias still
resolves — `tests/test_source_reactive.c` pins the list verbatim, because a
dropped binding does not crash, it silently reads 0.

Sampling is shared: `reactive_snapshot_t` is ~4KB, so all bound uniforms read
one process-wide snapshot refreshed at most once per frame, keyed on the
injected timestamp. Twenty bound uniforms cost one `reactive_get()`.

### GL objects (slice 3)

`include/neowall/gfx/gfx.h` is the bottom layer: `nw_gfx_program`,
`nw_gfx_texture`, `nw_gfx_target`. It knows OpenGL and knows nothing about
passes, channels, Shadertoy, or wallpapers.

The one piece of real behaviour is feedback. A pass that samples its own
previous frame needs two textures and a flip, and in the old code that was two
raw GLuints plus a `ping_pong_index` that callers indexed by hand — so "which
texture may I read this frame" was answered in several places and could
disagree. `nw_gfx_target` owns the pair and exposes exactly `read()` and
`write()`; no caller touches the array. Reading the texture you are writing is
undefined rather than an error on most drivers, so that bug surfaces as flicker
or wrong-looking decay, never a crash — `tests/test_gfx.c` pins it against a GL
stub so it is checkable with no display server.

Compile/link logs live on the program object rather than in one global "last
error" buffer, because with N passes a shared buffer means pass 3's failure
overwrites pass 1's before anyone reads it.

### The graph (slice 4)

`include/neowall/graph/graph.h` is where the remaining hardcoded lists go. The
old engine could not express "six passes", "nine inputs", or "bind this pass to
a weather feed", because each of those was an enum arm or an array bound. Here
they are lengths:

| old | new |
|---|---|
| `MULTIPASS_MAX_PASSES 5` | `nw_pass_vec`, any length |
| `PASS_TYPE_BUFFER_A..D` | a pass has a name; feedback is a flag |
| `MULTIPASS_MAX_CHANNELS 4` | `nw_binding_vec`, any length |
| `channel_source_t` (12 arms) | `nw_source *`, any provider |
| `uniform_locations_t` (~60 `GLint` fields) | `nw_uniform_vec`, looked up by name |

Execution order is a topological sort over the binding edges, so "buffers
before image" is a consequence of the dependencies rather than a hardcoded pass
ordering. Two rules matter and both are tested:

- **A self-edge is feedback, not a cycle.** A pass sampling its own previous
  frame is every accumulation buffer ever written; `gfx` resolves it through the
  ping-pong pair, so it imposes no intra-frame ordering constraint. Treating it
  as a dependency breaks every Buffer A shader in existence.
- **Any other cycle is a build error.** A reads B reads A within one frame is
  unsatisfiable, and refusing at build time means the render path never has to
  think about it.

Topology (`nw_graph`) is separate from GL state (`nw_graph_state`), so resize
and reload can throw the state away without touching the graph. Binding
resolution goes through `nw_gfx_target_read()`, which is what keeps a feedback
pass off the texture it is currently writing; an unbound slot resolves to 0 and
samples black rather than leaving whatever was last in the sampler unit.

## 6. The occlusion contract

This is the part that has to survive the rewrite, because it is the reason to
build a desktop canvas on neowall instead of on conky.

Every source is visibility-gated by the core:

```c
void nw_source_set_visible(nw_source *, bool);   /* gates tick() */
```

Hidden means: no ticks, no polls, no subprocesses, and `on_visibility(false)`
so providers holding real resources (threads, PTYs, capture streams) park
themselves. A covered output costs nothing — not just no GPU, no CPU either.

That gets enforced structurally rather than remembered. A provider cannot
accidentally keep polling behind a maximized window, because it is not the
thing that decides when it runs.

The claim this buys: *a full dashboard that costs zero the moment you are not
looking at it.* No other Linux desktop-widget system can say that.

## 7. Migration

Slices, each one shippable and green on its own. No big-bang branch.

| # | slice | state |
|---|---|---|
| 1 | `source/` — open data plane, registry, exec + file providers | **landed** |
| 2 | port `reactive.c`'s 24 binds to builtin providers, delete `uniform_bind_t` | **landed** |
| 3 | `gfx/` — GL objects out of the old engine | **landed** |
| 4 | `graph/` — N passes, M bindings, topo order | dropped: the engine's source model does this, `graph/` was deleted |
| 5 | replace the old engine | **landed**: the rewrite's source model + engine. `shader_multipass.c`, `multipass_parse.c`, `manifest.c`, `adaptive_scale.c`, `multipass_optimizer.c`, `shadertoy_compat.c` and `shader_diag.c` are deleted (the cache and the hints were rewritten); `uniform_bind_t` and `channel_source_t` died with them |
| 6 | a frontend vtable (Shadertoy as one frontend of several) | next |
| 7 | layers: viewport + blend per pass, the actual "canvas" | |

Compatibility: existing `.glsl` and `.neowall` files keep working, including
the old `bufferA { ch0 self }`, `channelN`, `texture:FILE` and `state` forms.
