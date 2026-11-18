# Contributing

Bug reports, shaders and patches are welcome. Open an
[issue](https://github.com/1ay1/neowall/issues) first for anything large, so
we can agree on the approach before you spend time on it.

## Build and run

```bash
meson setup build --buildtype=debug
ninja -C build
./build/neowall -f -v          # run in the foreground with debug logging
```

## Test

```bash
meson test -C build
```

The unit tests need no display. The `e2e` suite starts the real daemon under a
headless sway and under Xvfb and drives every control command; it is skipped
when those aren't installed. CI also runs everything under sanitizers, which
you can reproduce with clang:

```bash
CC=clang meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false
meson test -C build-asan

CC=clang meson setup build-tsan -Db_sanitize=thread -Db_lundef=false
meson test -C build-tsan --no-suite fuzz
```

## Before you send a patch

- `sh scripts/format.sh` formats the code (CI checks it).
- `scripts/check-layers.sh` checks that modules include only what they may
  (see [ARCHITECTURE.md](docs/ARCHITECTURE.md)).
- `scripts/check-unused.sh` and `scripts/version.sh check` must pass too.
- Add or extend a test for any bug you fix or behaviour you add.
- Write commit messages that say what changed and why.

## Shaders

New example shaders must be your own work or carry a license that allows
redistribution, with an SPDX header like the existing ones. Check them with:

```bash
neowall render myshader --check --bench --size 3840x2160
```

They should be free of NaN, and a few milliseconds per frame at 4K at most.
Shaders whose license doesn't allow redistribution belong in
`contrib/shaders-unlicensed/`, which is never installed.

## Where things are

| | |
|-|-|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Layout, threading, the runtime, conventions |
| [docs/internals/](docs/internals/) | The event loop, the backend interface, the shader engine, multi-monitor state |
| [docs/PACKAGING.md](docs/PACKAGING.md) | Releases and distribution packages |
