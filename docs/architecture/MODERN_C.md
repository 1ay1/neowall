# Toward a modern C architecture

A measured audit of where neowall is (0.8.0, ~39.5k lines of C excluding
vendored code) and a plan to move it to a modern C shape: C23, explicit
ownership, single-owner state, narrow module boundaries, and errors that
can't be dropped. Every number here was measured on the tree, not guessed.

This is a plan, not a description. Nothing in it has landed yet.

## Where it already is

The foundations are good, better than most C codebases this size:

- One public include tree, fully qualified includes, no `../` includes.
- `-std=c11 -Wall -Wextra -Wpedantic -Werror`, and `-Wshadow`,
  `-Wnull-dereference`, `-Wvla`, `-Wimplicit-fallthrough` are already clean.
- A single-threaded Elm-style runtime (`src/runtime/rt.c`): one thread owns
  state, workers only post messages. This is the modern answer to C
  concurrency and it's already in place.
- `nw_result`, `nw_vec`, `NW_DEFER` exist (`include/neowall/result.h`,
  `vec.h`, `defer.h`).
- CI runs ASan+UBSan, TSan, clang-tidy, `gcc -fanalyzer`, clang-format.
- Narrow seams exist where they were built recently: the compositor vtable,
  `gfx/`, `graph/`, `source/`, `frame_clock`, `shader_clock` (pure logic,
  unit tested).

## What's holding it back

| Problem | Measured |
|---------|----------|
| `struct output_state` is a god object | **110 fields**: GL handles, shader state, pacing, image slideshow, transitions, geometry, input, terminal, refcount, lifecycle |
| Long functions | `config_load_internal` 640 lines, `main` 630, `render_outputs` 469, `parse_wallpaper_config` 398, `multipass_render` 308, `render_frame_shader` 267 |
| Big files | `shader_multipass.c` 3039, `output.c` 2646, `x11_core.c` 2417, `config.c` 2216 |
| Config isn't immutable | 13 writes to `output->config->...` from outside `src/config/` (type, term size, cycle index) |
| Two error styles | 23 functions return `nw_result`, 266 return `bool` and log; 526 `log_error` calls. 9 `nw_result` returns are silently dropped (`output_set_shader` x4, `nw_source_register` x5) |
| Ownership by convention | 123 raw `malloc/calloc/realloc`, `NW_DEFER` used **0** times, 2 `goto cleanup` |
| Fixed buffers | 19 `char[PATH]` fields in config/output structs; 30 `strcat` calls building shader source in `shader_multipass.c` |
| Hidden globals | 74 mutable file-scope statics (20 in `reactive.c`, 8 in `x11_occlusion.c`, 7 in `hyprland_coverage.c`) |
| Layering leaks | `render.c` includes the Hyprland backend header; `output.c` includes 12 module headers; `neowall.h` (included by 41 files) pulls in `output.h`, `config.h`, EGL and GL; include cycles compositor\<-\>output, compositor\<-\>config, compositor\<-\>egl, config\<-\>shader; 9 `extern` declarations inside `.c` files |
| `.c` included into `.c` | `app_render.inc` is `#include`d into `app.c` |
| One link unit | No per-module libraries, so nothing stops a module reaching into any other |

Extra warnings, counted with the current code against a stricter set:
`-Wsign-conversion` 156, `-Wdouble-promotion` 58, `-Wconversion` 46,
`-Wswitch-enum` 41, `-Wmissing-prototypes` 36, `-Wcast-qual` 32. About 370 in
71 files, so worth fixing rather than suppressing.

## The target shape

```
                 runtime (rt)  ─── messages ───►  app: update / subscribe / present
                                                     │
     ┌──────────────┬──────────────┬─────────────────┼──────────────┬────────────┐
     ▼              ▼              ▼                 ▼              ▼            ▼
 config (pure)  display/        scene/           render/        sources/     platform/
 parse → value  outputs,        what each        GL only:       reactive,    compositor
 immutable      geometry,       output shows:    gfx, graph,    audio, nv,   backends
 snapshot       pacing,         image, shader,   shader         windows      (wayland,
                frame clock     terminal         programs                    x11)
```

Rules the shape enforces:

1. **Config is an immutable value.** `config_load` returns a new snapshot;
   reload swaps the pointer. Runtime choices (current cycle index, live
   terminal command, auto-sized cols/rows) move into the model where they
   belong.
2. **Each module owns its state and exposes an opaque handle.** No module
   reads another's struct fields.
3. **Only `render/` calls GL; only `platform/` calls Wayland/X11.** Today
   that's nearly true already (one GL call leaks into `x11_core.c`, and
   `render.c` reaches into Hyprland); make it enforced.
4. **Errors are values and can't be ignored.** `[[nodiscard]]` on
   `nw_result`.
5. **Ownership is visible in the type.** Owning pointers are freed by
   `NW_DEFER` or a single `*_destroy`, borrowed ones are `const`.

## The plan

Ordered so each step is small, keeps the tree green, and makes the next one
cheaper. Effort is rough: S = an afternoon, M = a few days, L = a week+.

### Phase 1: the compiler does the policing (S)

1. **Move to C23.** gcc 16 and clang 23 build it here. The catch is CI's
   Debian bookworm jobs, which use the system gcc 12: it only has
   `-std=c2x`, without `constexpr`, `auto` inference or `<stdckdint.h>`. So
   either bump that job to trixie (gcc 14) or set `c_std=c2x` and use only
   what gcc 12 has: `[[nodiscard]]`, `typeof`, `nullptr`, `bool`/`true` as
   keywords, `static_assert` without the underscore. `[[nodiscard]]` is the
   one that matters most and works everywhere.
2. **`[[nodiscard]]` on every function returning `nw_result`**, via one
   macro in `result.h`. That immediately flags the 9 dropped results; fix
   each (log, retry, or `(void)` with a comment).
3. **Ratchet warnings.** Add `-Wmissing-prototypes` (36: make helpers
   `static` or declare them), `-Wswitch-enum` (41), `-Wcast-qual` (32),
   then `-Wconversion -Wsign-conversion` (~200, mostly int/size_t and
   float/double in render and image code). One flag per commit, each green.
4. **Kill the 9 in-file `extern`s**: include the real header.

### Phase 2: make boundaries real (M)

5. **Per-module static libraries in meson**: `nw_foundation`, `nw_config`,
   `nw_runtime`, `nw_render`, `nw_shader`, `nw_platform_wayland`,
   `nw_platform_x11`, `nw_terminal`, `nw_sources`. Each declares its
   dependencies; a module that reaches past them stops linking. Tests link
   only the library they test (today `test_terminal_wake` pulls in 4
   source files by hand).
6. **Break the include cycles.** `neowall.h` shrinks to the app state and
   stops including `output.h`, `config.h`, EGL and GL. Modules that need
   output geometry include `output.h` themselves. Forward-declare structs
   in headers instead of including.
7. **`app_render.inc` becomes `src/app/present.c`** with a small internal
   header. The `.c`-in-`.c` was a migration shortcut.
8. **Window rects through `sources/`, not `render.c` → Hyprland.** The
   shader gets windows the same way it gets CPU load: from a provider.

### Phase 3: split the god object (L, the big one)

9. **Split `output_state` (110 fields) by owner:**

   | New struct | Owner | Fields today |
   |------------|-------|--------------|
   | `nw_display` | platform | native handles, geometry, scale, connector name |
   | `nw_pacing` | app | pace\_\*, frame\_clock, frame timer, fps\_\* |
   | `nw_scene` | scene | a tagged union: `image`, `shader`, `terminal` |
   | `nw_render_target` | render | EGL surface, GL programs, textures, VAO, gl\_state cache |
   | `nw_output` | app | the refcounted handle that ties the four together |

   The tagged union is the real win: an image output can't carry shader
   fields and a terminal can't carry slideshow fields. Today every output
   carries all of them. Each piece is introduced as an embedded struct
   first (`o->pacing.period_ns`), so it's a mechanical rename per commit;
   then the embedding turns into ownership.

10. **Config as an immutable snapshot**: move the 13 runtime writes out of
    `output->config` into the scene/model, then make the snapshot
    `const`.

11. **Break up the long functions.** `render_outputs` (469 lines) has clear
    phases already marked in comments (snapshot, commands, per-output draw,
    swap, cleanup); each becomes a function. Same for `config_load_internal`
    (640) and `main` (630, mostly CLI dispatch that can be a table).

### Phase 4: memory and strings (M)

12. **An arena per load.** Config parse, manifest parse and shader source
    assembly all allocate many small things with one lifetime. An arena
    (`nw_arena`, a bump allocator with one `free`) replaces dozens of
    `malloc`/`free` pairs and makes leaks structurally impossible there.
13. **A string builder** (`nw_strbuf`: length-tracked, grows, never
    overflows) replaces the 30 `strcat` calls that assemble shader source,
    and the fixed `char[PATH]` fields become owned strings.
14. **Use `NW_DEFER`** where a function has more than one exit and owns a
    resource. It exists and is used zero times.
15. **Checked arithmetic** for every size computed from untrusted input:
    image dimensions, buffer sizes, terminal grid sizes. `ckd_mul` /
    `ckd_add` from `<stdckdint.h>` once the compiler floor allows it;
    `__builtin_mul_overflow` (gcc and clang both have it) until then.

### Phase 5: state and globals (M)

16. **Move file-scope globals into owned contexts.** `reactive.c`'s 20
    statics become one `nw_reactive` object created by the app; same for
    `x11_occlusion` (8) and `hyprland_coverage` (7). Makes them testable
    and makes "who owns this" a type question.
17. **Threads as typed mailboxes.** Workers already post to the loop; make
    the snapshot handoff (reactive, audio, nvidia, preload) one generic
    `nw_publish(T)` pattern instead of four hand-rolled mutex+flag pairs.

## Order and payoff

| Step | Effort | Risk | Payoff |
|------|--------|------|--------|
| 1–4 compiler policing | S | low | catches real bugs now, cheap to keep |
| 5–8 module libraries, cycles | M | low | the architecture stops eroding |
| 9–11 split output_state | L | medium | the biggest readability and safety win |
| 12–15 arenas, strbuf, defer | M | low | removes whole classes of memory bugs |
| 16–17 globals, mailboxes | M | low | testable, clear ownership |

Start with phase 1: it's mechanical and the results show up immediately.
Phase 3 is where the real change is, and phases 1–2 make it safe to do.

## What not to do

- **Don't rewrite.** Every step above keeps the tree building and the tests
  green; the runtime rewrite showed that incremental migration works here.
- **Don't add a generic object system** (vtables everywhere, reference
  counting everything). Use vtables only at real plugin seams (compositor
  backends, sources, frontends), and plain structs everywhere else.
- **Don't chase `-Weverything`.** Pick warnings that find bugs (conversion,
  switch-enum, missing-prototypes, cast-qual) and leave pure style ones off.
