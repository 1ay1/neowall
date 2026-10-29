# Changelog

All notable changes to neowall. Dates are release dates.

## Unreleased

### Fixed
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
- Fuzzers for the terminal, EXIF, image decoding and shader-source
  transforms; all fuzzers run in `meson test` and in CI.
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
