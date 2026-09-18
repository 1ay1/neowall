# Shader engine v2 — a render graph with pluggable data

Status: design, in progress. Slice 1 (the source layer) has landed —
`include/neowall/source/source.h`, `src/source/`, `tests/test_source.c`
(103 checks, headless, ASan+UBSan clean).

This replaces the whole of `src/shader/` over several steps. Read this before
touching anything in there.

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
| 2 | port `reactive.c`'s 24 binds to builtin providers, delete `uniform_bind_t` | next |
| 3 | `gfx/` — pull GL object handling out of `shader_multipass.c` | |
| 4 | `graph/` — N passes, M bindings, topo order; `channel_source_t` dies | |
| 5 | `frontend/shadertoy.c` — move the parser behind the vtable | |
| 6 | `glsl/` — prelude + stdlib + shadow, currently three places | |
| 7 | `engine/` — adaptive + optimizer as policy over the graph | |
| 8 | layers: viewport + blend per pass, the actual "canvas" | |

Old file → new home:

```
shader_multipass.c    -> gfx/ + graph/ + engine/ + frontend/shadertoy.c
multipass_parse.c     -> frontend/shadertoy.c
shadertoy_compat.c    -> frontend/shadertoy.c
manifest.c            -> frontend/manifest.c
reactive.c            -> source/builtin_*.c
adaptive_scale.c      -> engine/adaptive.c
render_optimizer.c    -> engine/throttle.c
multipass_optimizer.c -> engine/throttle.c
glsl_shadow.c         -> glsl/shadow.c
shader_stdlib.h       -> glsl/stdlib.h
program_cache.c       -> gfx/program_cache.c
shader_clock.c        -> source/builtin_clock.c
```

Compatibility: existing `.glsl` and `.neowall` files keep working. The
Shadertoy frontend is a superset of what the current parser accepts, and the
manifest grammar only gains `name(args)` provider syntax.
