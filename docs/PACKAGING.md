# Packaging & Releasing NeoWall

This is both the guide for distro packagers and the runbook for cutting a
release. The short version: **`meson.build` owns the version number, and
everything downstream is generated from it.** Nothing is hand-edited.

---

## Cutting a release

```bash
# 1. Bump the version. This rewrites meson.build AND every derived file
#    (flake.nix, packaging/PKGBUILD) in one shot.
scripts/version.sh set 0.5.3

# 2. Sanity-check that nothing is out of sync.
scripts/version.sh check

# 3. Build + test locally before tagging.
meson setup build --wipe && ninja -C build && meson test -C build

# 4. Commit, tag, push.
git commit -am "release: neowall 0.5.3"
git tag -a v0.5.3 -m "neowall 0.5.3"
git push origin main --follow-tags
```

Pushing the `v*.*.*` tag is the only trigger needed. From there CI does the rest.

### What CI does with the tag

`.github/workflows/release.yml` fans the tag out to:

| Job | Produces |
|-----|----------|
| `build-arch` | `.pkg.tar.zst` + `neowall-linux-x86_64-arch.tar.gz` |
| `build-debian` | `.deb` for Debian bookworm |
| `build-ubuntu` | `.deb` for Ubuntu 24.04+ |
| `build-fedora` | `.rpm` |
| `build-opensuse` | `.rpm` (non-blocking; Leap's Mesa lags) |
| `build-appimage` | portable `.AppImage` |
| `release` | GitHub Release with all of the above + `SHA256SUMS.txt` |
| `publish-aur` | pushes refreshed `neowall-git` and `neowall-bin` to the AUR |
| `publish-nix` | commits the synced version references back to `main` |

`publish-aur` and `publish-nix` both `needs: release`, so they only run once the
release itself succeeded.

---

## Keeping downstream packages from going stale

This used to be a real failure mode: a release would ship, the GitHub Release
page would look perfect, and the AUR would keep serving a version from months
earlier because nothing ever pushed to it. Three mechanisms now prevent that.

### 1. One source of truth (`scripts/version.sh`)

```bash
scripts/version.sh get          # print the current version
scripts/version.sh set 0.5.3    # set meson.build + propagate everywhere
scripts/version.sh sync         # re-propagate whatever meson.build says
scripts/version.sh check        # exit 1 if anything disagrees
```

`meson.build` is authoritative. `flake.nix` and `packaging/PKGBUILD` are
derived. There is no third place to forget.

### 2. A CI gate (`quality.yml` → `version-consistency`)

Every push and PR runs `scripts/version.sh check`. A commit that bumps
`meson.build` without re-syncing the derived files fails CI immediately, so
drift can never reach `main` in the first place.

### 3. A daily watchdog (`packaging-freshness.yml`)

Once a day the workflow queries the AUR RPC and compares `neowall-git` /
`neowall-bin` against the newest GitHub Release. If either lags — or is missing
entirely — it opens (or comments on) a single rolling `packaging` issue. It also
runs on `workflow_dispatch`, so you can check on demand:

```bash
gh workflow run packaging-freshness.yml
```

### Required secret

`publish-aur` needs an AUR SSH key in the repository secret
**`AUR_SSH_PRIVATE_KEY`**:

```bash
# Generate a dedicated key (do NOT reuse your personal one).
ssh-keygen -t ed25519 -N '' -C 'neowall-ci' -f ~/.ssh/neowall_aur

# Register the PUBLIC half at https://aur.archlinux.org/account/ -> SSH keys.
cat ~/.ssh/neowall_aur.pub

# Store the PRIVATE half as the repo secret.
gh secret set AUR_SSH_PRIVATE_KEY < ~/.ssh/neowall_aur
```

The publish job **skips with a warning** rather than failing when the secret is
absent, so forks can still cut releases. The daily watchdog is what catches the
resulting drift, so don't rely on the skip being noticed on its own.

---

## For distro packagers

### Build

```bash
meson setup build --prefix=/usr --buildtype=release
ninja -C build
meson test -C build            # headless; no display server required
DESTDIR="$pkgdir" ninja -C build install
```

### Runtime dependencies

| Library | Purpose | Required? |
|---------|---------|-----------|
| `wayland-client`, `wayland-egl`, `wayland-cursor` | Wayland backend | one backend required |
| `libX11`, `libXrandr` | X11 backend | one backend required |
| EGL + desktop OpenGL (`libGL`) | rendering | yes |
| `libpng`, `libjpeg-turbo` | image wallpapers | yes |
| **`libxkbcommon`** | keyboard input for the terminal wallpaper | **strongly recommended** |

`libxkbcommon` is an *optional* meson dependency, which makes it easy to miss:
build without its headers and everything still compiles, but the terminal
wallpaper silently loses keyboard input (`NEOWALL_HAVE_XKB` is never defined).
Every official package installs it. If you are packaging NeoWall yourself,
install `libxkbcommon-dev` / `libxkbcommon-devel` at build time and depend on
the runtime library.

Build-time extras: `meson`, `ninja`, `pkg-config`, `wayland-protocols`.

### Install layout

```
/usr/bin/neowall
/usr/share/neowall/shaders/...
/usr/share/applications/neowall.desktop
/usr/share/icons/hicolor/scalable/apps/neowall.svg
/usr/lib/systemd/user/neowall.service
/usr/share/doc/neowall/{README.md,LICENSE}
```

### Arch

`packaging/PKGBUILD` in this repo is the canonical `neowall-git` PKGBUILD and is
pushed to the AUR verbatim by CI. Edit it here, not on the AUR — a manual AUR
edit will be overwritten by the next release.

### Maintaining a package elsewhere?

Open an issue and we'll link it in the README, and add it to the freshness
watchdog so it doesn't quietly rot.
