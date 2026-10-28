# Community shader ports (not installed)

These are ports of shaders from Shadertoy and elsewhere. They work with
neowall, but their licenses either forbid redistribution, are
non-commercial (CC BY-NC-SA, Shadertoy's default), or aren't stated, so
`meson install` doesn't install them and distro packages don't ship them.

To use one, copy it into your own shader directory:

```sh
cp contrib/shaders-unlicensed/neonwave_sunrise.glsl ~/.config/neowall/shaders/
```

then reference it from your config with `shader neonwave_sunrise.glsl`.

Where a file's header names the original author or Shadertoy link, keep it.
If you're the author of one of these and want it relicensed or removed,
open an issue.
