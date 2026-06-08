# TODO Index

Snapshot of `TODO`/`FIXME` markers across the tree, grouped by area. Useful
fodder for drive-by contributors: pick a row, file an issue, send a PR.

Generated from `grep -rnE '\b(TODO|FIXME|XXX|HACK)\b' src/`. Re-generate by hand
when this drifts; eventually we'll make it a CI job.

## Wayland backends

### GNOME Shell backend (stub)

Source: `src/compositor/backends/wayland/compositors/gnome_shell.c`

The GNOME path is a stub — Mutter does not expose wlr-layer-shell, so neowall
falls back here. The TODOs trace what a real implementation would need:

- Bind `xdg_wm_base` and create an `xdg_toplevel` desktop window pinned to
  the bottom of the stack.
- Implement subsurface fallback so neowall draws under desktop icons rather
  than over them.
- Wire `xdg_surface.configure` handler so size changes propagate to the
  render path.

Scope: large. Anyone interested in proper GNOME support, this is the file.

### Compositor README placeholders

Source: `src/compositor/README.md`

Two `TODO:` markers in the docs flag spots where the prose drifts from the
implementation. Lower-priority polish — match the doc to the actual op vtable.

## Conventions

- `TODO`: future work, scope-clear.
- `FIXME`: known bug, has a workaround in place.
- `XXX`: surprising-but-correct code that warrants a second look.
- `HACK`: deliberate compromise; should be retired.

If you remove a marker, also update this file.
