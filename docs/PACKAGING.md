# Packaging Staticwall

A practical guide for package maintainers and distribution packagers.

## For Package Maintainers

If you want to package Staticwall for your distribution, thank you! Here's what you need to know.

## Build Requirements

**Compile-time:**
- C compiler (gcc or clang)
- make
- pkg-config
- wayland-protocols
- wayland development headers
- EGL development headers
- OpenGL ES 2.0 development headers
- libpng development headers
- libjpeg development headers

**Runtime:**
- wayland
- EGL
- OpenGL ES 2.0
- libpng
- libjpeg

## Building

```bash
make                    # Build
make install           # Install to /usr/local
make install PREFIX=/usr  # Install to /usr
```

## Installation Paths

Default installation with `make install`:

```
/usr/local/bin/staticwall                              # Binary
/usr/local/share/staticwall/config.vibe.example        # Example config
/usr/local/share/staticwall/default.png                # Default wallpaper
```

The user's config will be created at first run:
```
~/.config/staticwall/config.vibe                       # User config
~/.local/share/staticwall/default.png                  # User wallpaper (copied)
```

## Systemd Service

A user service file is provided at `packaging/systemd/staticwall.service`.

Install to: `/usr/lib/systemd/user/staticwall.service`

Users can then enable it with:
```bash
systemctl --user enable staticwall.service
systemctl --user start staticwall.service
```

## Creating an Arch Package (AUR)

### Quick Start

1. **Create a separate git repo for AUR:**
```bash
git clone ssh://aur@aur.archlinux.org/staticwall-git.git
cd staticwall-git
```

2. **Create PKGBUILD:**
```bash
# Maintainer: Your Name <your-email@example.com>

pkgname=staticwall-git
pkgver=r123.abc1234
pkgrel=1
pkgdesc="A reliable Wayland wallpaper daemon written in C"
arch=('x86_64' 'aarch64')
url="https://github.com/1ay1/staticwall"
license=('MIT')
depends=('wayland' 'egl-wayland' 'mesa' 'libpng' 'libjpeg-turbo')
makedepends=('base-devel' 'wayland-protocols' 'pkg-config' 'git')
provides=('staticwall')
conflicts=('staticwall')
source=("${pkgname}::git+https://github.com/1ay1/staticwall.git")
sha256sums=('SKIP')

pkgver() {
    cd "${srcdir}/${pkgname}"
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cd "${srcdir}/${pkgname}"
    make
}

package() {
    cd "${srcdir}/${pkgname}"
    install -Dm755 build/bin/staticwall "${pkgdir}/usr/bin/staticwall"
    install -Dm644 config/staticwall.vibe "${pkgdir}/usr/share/staticwall/config.vibe.example"
    install -Dm644 assets/default.png "${pkgdir}/usr/share/staticwall/default.png"
    install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/staticwall/LICENSE"
    install -Dm644 packaging/systemd/staticwall.service "${pkgdir}/usr/lib/systemd/user/staticwall.service"
}
```

3. **Generate .SRCINFO and publish:**
```bash
makepkg --printsrcinfo > .SRCINFO
git add PKGBUILD .SRCINFO
git commit -m "Initial commit"
git push
```

### Testing Locally

```bash
makepkg -si      # Build and install
staticwall --version
```

## Creating Packages for Other Distros

### Debian/Ubuntu

Key points:
- Use `dpkg-buildpackage` or `debuild`
- Create `debian/control`, `debian/rules`, `debian/changelog`
- Dependencies: `libwayland-dev`, `libegl1-mesa-dev`, `libgles2-mesa-dev`, `libpng-dev`, `libjpeg-dev`

### Fedora/RHEL

Key points:
- Create `.spec` file
- Use `rpmbuild`
- Dependencies: `wayland-devel`, `mesa-libEGL-devel`, `mesa-libGLES-devel`, `libpng-devel`, `libjpeg-turbo-devel`

### NixOS

Add to nixpkgs with:
- `stdenv.mkDerivation`
- `nativeBuildInputs = [ pkg-config ]`
- `buildInputs = [ wayland wayland-protocols mesa libpng libjpeg ]`

### Gentoo

Create ebuild in overlay:
- `EAPI=8`
- `DEPEND="dev-libs/wayland media-libs/mesa media-libs/libpng media-libs/libjpeg-turbo"`

## Package Naming

Recommended package names:
- `staticwall` - For stable releases
- `staticwall-git` - For latest git version

## Notes for Packagers

- **No root required:** Staticwall runs as a normal user
- **Config is optional:** Works with defaults if no config exists
- **First-run setup:** Automatically creates user config directory
- **Clean uninstall:** Remove `~/.config/staticwall/` and `~/.local/share/staticwall/`

## Versioning

- Follows semantic versioning: `MAJOR.MINOR.PATCH`
- Check `include/staticwall.h` for `STATICWALL_VERSION`
- Git tags are prefixed with `v`: `v0.1.0`, `v0.2.0`

## Support

- **Issues:** https://github.com/1ay1/staticwall/issues
- **Packaging questions:** Open an issue with `[packaging]` in the title

## Current Packages

- **AUR:** `staticwall-git` (if published)
- Want to maintain a package? Let us know!

---

**Thank you for packaging Staticwall!** Package maintainers are the unsung heroes of open source.