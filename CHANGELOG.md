# Changelog

All notable changes to neowall. Dates are release dates.

## Unreleased

### Fixed
- A wallpaper paused by a fullscreen or covering window sometimes stayed
  frozen after the window closed. Occlusion was only re-checked when the
  compositor happened to send an event, and uncovering needn't produce one.
  It is now re-checked every 250 ms while an output is covered (or could
  be), so a wallpaper comes back within a quarter second.
- The frame-callback watchdog replaces a request the compositor never
  answers instead of waiting on it forever, so a dropped callback can't
  pin an output as covered.
- Hyprland: a fullscreen window on a workspace you switched away from no
  longer keeps the wallpaper paused (Hyprland's own view of the active
  workspace now wins over foreign-toplevel state). Coverage is computed in
  layout coordinates, so scaled (HiDPI) and rotated monitors pause
  correctly.
- X11: a minimised or off-desktop fullscreen window no longer counts as
  covering, and losing `_NET_CLIENT_LIST_STACKING` (WM restart) uncovers
  every output instead of leaving them paused.
- Turning `pause_on_fullscreen` off in a reload un-pauses an output that
  was covered at the time.

## 0.11.0 - 2026-11-18

### Added
- `pause-shader --ease-ms MS` glides the animation to a stop before
  freezing, and `resume-shader --ease-ms MS` speeds it back up from the exact
  frame it stopped on (#86, #87, thanks @eikooc). The speed follows a
  minimum-jerk curve, either command can interrupt an ease in progress with
  no visible step, and spanned monitors ease in lockstep. The ease is capped
  at 60 s.
- `shader_pause_ease_ms` (config) sets the default ease, so a battery hook
  can stay a bare `neowall pause-shader`.

### Fixed
- A shader loaded while the animation was paused (reload, `next`, a monitor
  plugged in) never drew, so the new wallpaper didn't appear until resume.
  It now shows its first frame and stays paused.

## 0.10.3 - 2026-11-18

### Added
- `allow_exec true` (config) and `--allow-exec` (flag) enable `exec()`
  manifest uniforms. The feature was documented, but nothing could turn it on.
  It stays off by default, and a shader cannot enable it on its own.

### Fixed
- `shader_fps` was ignored with vsync on (the default). It now caps the frame
  rate while still presenting tear-free; unset, vsync follows the monitor.
- `neowall current` and `neowall list` reported the last wallpaper as active
  after the daemon had exited. They now say no daemon is running.
- `neowall render -o /dev/null` (or any pipe or device) works again.
- `--help` and `--version` no longer say neowall is Wayland-only, and the
  unmeasured "60 FPS" and "2% CPU" claims are gone.

### Documentation
- Rewritten and consolidated: a shorter README, `docs/CONFIGURATION.md` for
  every option, `docs/SHADERS.md` for shader authoring, `CONTRIBUTING.md`,
  and backend details under `docs/internals/`. Stale design notes are gone,
  and the sample config and man pages match the code.

## 0.10.2 - 2026-11-18

### Added
- `neowall render --video FILE` records a clip (mp4, mkv, webp or gif) at an
  exact frame rate with `--duration`/`--start`; `--mouse-path` glides the
  pointer across it.

### Fixed
- The backend's native-window callback was called through a function pointer
  of the wrong type (pointer vs integer `EGLNativeWindowType`): undefined
  behaviour that made sanitizer builds abort at startup. CI now builds the
  sanitizer job with clang, which catches this class of bug.
- GPU timer queries used by `render --bench` were never freed.
- `render --sheet` with fewer frames than tiles left most tiles black.
- `render` writes PNGs atomically, rejects oversized `--size`/`--sheet`
  cleanly, frees GPU objects on every error path, and keeps iDate and
  iTimeOfDay in step on daylight-saving days.
- Hyprland window coverage: one cache slot per monitor (multi-monitor setups
  recomputed every frame).
- `neon_city`: the camera could orbit into a tower and turn the screen solid
  green.
- NULL checks in Wayland surface creation and offscreen target setup.
- CLI commands wait up to 10 s for a busy daemon (was 3 s).

## 0.10.1 - 2026-11-18

### Changed
- `tokyo_night`, a rainy neon Tokyo skyline in the Tokyo Night palette, is
  the new default wallpaper. It has seven passes with quarter-resolution clouds and bloom, follows
  the real clock, parallaxes with the mouse, and costs ~1.2 ms per frame at
  3440x1440.

### Fixed
- Builds on musl (Alpine): the GPU timer query used by `render --bench` is
  looked up at runtime instead of linked, as libGL there doesn't export it.
- `-Werror` builds of the test suite: the theme-palette GLSL chunk is now in
  the stdlib namespace audit (test_glsl_shadow).

## 0.10.0 - 2026-11-18

### Added
- `neowall render <shader>`: draw a shader offscreen to a PNG on a headless
  EGL context. It needs no compositor or daemon (an EGL device, so NVIDIA works),
  and it never touches a running daemon. `--size`, `--time`, `--sheet N`
  (contact sheet over time), `--passes` (every buffer as a PNG), `--at HH:MM`
  (time of day), `--mouse`, `--set NAME=VALUE` (pin any float uniform),
  `--palette`. Inputs are pinned to calm values unless `--live`, so renders are
  deterministic.
- `neowall render --bench`: real GPU milliseconds per pass (timer queries) and
  what that costs at 30/60 fps. `--check`: finds NaN/Inf pixels in every pass,
  names the pass they start in with a bounding box, paints them magenta, and
  exits 2.
- `palette` config key and `iPalette`/`iPaletteSize` with `nwPal()`,
  `nwPalBg()`, `nwPalFg()`, `nwPalAccent()`, `nwPalSmooth()`: shaders follow
  the desktop colour theme. Inline colours or any file containing `#rrggbb`
  (kitty, ghostty, CSS themes work as is).
- Shader names work without an extension everywhere (`shader garden`,
  `neowall render garden`); `.neowall` is preferred over `.glsl`.

### Fixed
- `neowall preview`/`watch` wrote the daemon's state file, so after a preview
  exited `neowall current` kept reporting the preview's shader.
- Declaring a manifest uniform yourself (`uniform float uGlow;`), as the
  docs suggest, failed with "declaration conflicts with previous declaration".
- Release tarballs and `neowall-bin` shipped only the binary: no example
  shaders, and the first-run config pointed at a missing
  `~/Pictures/wallpaper.png` (a black screen). The tarballs now carry
  `share/neowall` (found next to the binary, installed to `/usr/share/neowall`
  by the package), and a bare binary writes a built-in shader on first run.

### Performance
- Audio analysis is skipped in silence (it decays exactly as a silent
  spectrum would) and entirely while no output is showing the wallpaper. It
  was ~10% of the daemon's CPU even when nothing was playing.
- Hyprland window coverage is computed once per IPC snapshot instead of
  every frame. A covered shader wallpaper now idles at ~0.1% CPU.

### Docs
- SHADER_NOTES: an authoring workflow built on `render`/`--bench`/`--check`, theme
  palettes, and the per-pass `scale`/`format`/`filter`/`wrap`/`size` keys,
  which existed but were undocumented.

## 0.9.0 - 2026-11-14

### Fixed
- A reload no longer stops the wallpaper while the new shader compiles: the
  old shader keeps drawing and the daemon keeps answering, and the new
  config goes live together with its shader. `neowall reload` still waits
  for the real outcome, so a shader that doesn't compile is reported as a
  failed reload, with the previous one left on screen.
- X11: any stray X protocol error (a window going away mid-query) ended the
  daemon, since Xlib's default handler exits. It's now logged and drawing
  goes on. The fullscreen check read window properties without checking
  their type, and ran 6-24 times a second depending on refresh rate; it's
  4 a second now. With mouse input on, the pointer was queried once per
  output per frame (a round trip each); now once per frame.
- `neowall set <index>` on a shader cycle froze the screen while the new
  shader compiled; it builds in the background now, like timed cycling.
- `neowall reload` could report "the daemon didn't answer" on a slow GPU,
  because the reply waits for the new shader to compile.
- An exec source whose command printed its value and kept running (or
  closed stdout early, or never stopped printing) froze the whole wallpaper:
  the render loop waited for it to exit. Reaping never blocks now, and the
  command's whole process group is killed, so a child it forked can't
  outlive it.
- The FPS overlay made a new GL buffer and ~250 uploads every frame, and
  could leave blending on; image wallpapers left blending on for the next
  draw. Every redraw of an image also logged at INFO and synced the GPU
  with glGetError.
- An idle wallpaper wasn't idle: a frame-callback loop woke the daemon at
  the display's refresh rate (125 wakeups/s for a static image), and the
  system sampler read /proc and /sys four times a second with nothing
  reading it. A static image now uses no CPU and has no wakeups.
- Shaders that read audio stopped starting capture after the shader engine
  rewrite, so audio wallpapers sat still. nvidia-smi now starts only for a
  shader that reads `iNv*`, instead of always.
- Child processes (terminal wallpapers, exec sources, parec, nvidia-smi)
  inherited the daemon's fds, including other terminals' pty masters.
  They now start with only stdin, stdout and stderr.
- A tiny image file claiming a huge size (a 140-byte JPEG claiming
  16391x4101, found by fuzzing) was decoded in full: ~400 MB and a second
  per slideshow step. Such files are refused. Large JPEGs are decoded at
  1/2 to 1/8 size when that still covers the screen (a 24 MP photo for
  1080p: 99 MB down to 34 MB).
- Terminal: `CSI 0 @`, `CSI 0 X` and `CSI 0 P` did nothing instead of acting
  on one cell, and could erase a wide glyph next to the cursor.
- Each frame asked EGL five times what was current; glvnd answers with a
  syscall each. neowall now tracks it.
- Shutdown and the 4 Hz system sampler waited on the wall clock, so a clock
  change could stall them. Timelapse moved once a second and jolted on NTP
  steps; the timezone was read once at startup.
- The pid file error said "already running" when it just couldn't be
  created.
- On NVIDIA every wallpaper was black (at a steady 60 fps) since the GPU
  reset work: the GL context was first bound with no window, and the
  driver kept drawing to no buffer after a window was bound. The e2e test
  now checks the screen's pixels, not just the frame counter.
- Terminal wallpaper: a cursor saved before a monitor shrink could write past
  the cell grid when restored (heap overflow). A long OSC number no longer
  overflows. Both found by fuzzing.
- Under systemd, neowall no longer forks; the unit was killing the real
  daemon right after start.
- GNOME on Wayland now exits with a clear message instead of starting a
  backend that could never show anything.
- A per-output config block was applied on top of `default`, so the output
  was set up (and its shader compiled) twice on every load and reload.
- A shader could be refused on a backend whose surface wasn't a
  wl_egl_window or an X11 window, though an EGL surface existed.
- A monitor that appeared before the compositor had sized it was dropped
  for good ("no dimensions"); it now waits and comes up once sized.
- X11: the outputs were never freed on shutdown.
- `neowall watch` / `preview` printed their banner without flushing, so in
  a log file it showed up late or not at all.
- A corrupt or truncated PNG/JPEG leaked its decode buffers every time it
  came round in a slideshow (found by fuzzing).
- The color-emoji font reader read past the end of malformed fonts (any
  `term_font` a user downloads); every offset is bounds-checked now and
  embedded images are size-capped (found by fuzzing).

### Removed
- The unregistered generic Wayland fallback backend (dead code).
- The render optimizer (2.1k lines). Its GL state cache was wrong on a
  shared context: after a transition or the FPS overlay left blending on, a
  translucent shader drew blended over the previous frame. State is set
  directly each frame now, with no measurable FPS change.
- 74 functions nothing called. CI now fails on new ones
  (`scripts/check-unused.sh`).

### Added
- GPU reset and lost-surface recovery: the GL context is created robust,
  resets are detected, and neowall restarts cleanly (exit 75; re-execs itself
  outside systemd, at most 3 times a minute).
- `NEOWALL_GL_DEBUG=1` logs OpenGL driver messages.
- The systemd unit is `Type=notify`, with readiness, reload and stop reported.
- Man pages `neowall(1)` and `neowall.vibe(5)`, AppStream metainfo.
- Fuzzers for the terminal, EXIF, image decoding, emoji fonts and
  shader-source transforms; all run in `meson test` and in CI.
- The shader engine and the output/render path are tested end to end with no
  display: a surfaceless Mesa GL context renders real shaders and images and
  the pixels are checked. CI fails if these tests skip.
- The adaptive resolution controller is pinned by a golden trace of every
  decision across modes and load shapes.
- An end-to-end test runs the real daemon on a real compositor (sway, or
  nested Hyprland) and on X11 (Xvfb), through every control command, a
  terminal wallpaper, monitor hotplug and `neowall watch`, on both the
  layer-shell and KDE backends. CI runs it under ASan and TSan.
- Every image transition and the Hyprland window-coverage logic have tests.
- `neowall ping`: exit 0 only if a daemon answers.

### Changed
- Only freely licensed shaders are installed. Ports whose license forbids
  redistribution or isn't stated moved to `contrib/shaders-unlicensed/`.
  The default wallpaper is now `garden`.
- Shaders and sample configs are found through `$XDG_DATA_HOME`,
  `$XDG_DATA_DIRS` and the install prefix, not hardcoded `/usr` paths.
- Wayland protocol code is generated at build time by `wayland-scanner`.
  New build deps: `wayland-protocols` >= 1.31 and `wayland-scanner`.
- Warnings are errors only with `-Dwerror=true`. Clang builds cleanly.
- Builds are reproducible (no embedded timestamps).
- The desktop file and icon are named `io.github._1ay1.neowall`.

### Internal
- The code is split into modules with a strict include hierarchy and no
  cycles, checked in CI by `scripts/check-layers.sh`. `neowall.h` is gone.
- `output.c` and the X11 and wlroots backends are split into smaller files
  by role. Backends are a `const` table instead of registering at startup.
- The tree is clang-format clean, checked in CI (`scripts/format.sh`).

## 0.8.0 - 2026-10-26

- New event loop: a small Elm-style runtime replaces the old 1,400-line loop.
- The CLI talks to the daemon over a unix socket.
- Frame clock stamps `iTime` with the vblank the frame is shown at.
- Shader authoring: `neowall new`, `preview`, `watch`, `docs`.
- Shaders can see where your windows are; new `window_forge` and
  `window_light`.
- An open data plane: shaders bind live inputs by name instead of a fixed
  list, and can load their own images as channels.
- A scene kit for 3D wallpapers, with materials, reflections and shadows.
- Fixes for two data races, X11 vsync pacing, and children inheriting
  blocked signals.

## 0.7.2 - 2026-09-22
## 0.7.1 - 2026-09-16
## 0.7.0 - 2026-09-09

See the [GitHub releases](https://github.com/1ay1/neowall/releases) for
these and earlier versions.
