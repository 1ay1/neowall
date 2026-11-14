# Changelog

All notable changes to neowall. Dates are release dates.

## Unreleased

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
