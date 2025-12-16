# Packaging Files for NeoWall

This directory contains files needed by package maintainers.

## What's Here

- **`systemd/neowall.service`** - Systemd user service file
- **`neowall.install`** - Optional Arch Linux post-install script

## For Package Maintainers

If you're packaging NeoWall for a distribution, see:
- **[docs/PACKAGING.md](../docs/PACKAGING.md)** - Complete packaging guide

### Quick Reference

**Build:**
```bash
meson setup build --prefix=/usr
ninja -C build
DESTDIR="$pkgdir" ninja -C build install
```

**Systemd service install location:**
```
/usr/lib/systemd/user/neowall.service
```

**Arch .install file usage:**
Add to PKGBUILD:
```bash
install=neowall.install
```

## Creating AUR Package

PKGBUILD files are **NOT** kept in this repo. Create them in a separate AUR git repository:

```bash
git clone ssh://aur@aur.archlinux.org/neowall-git.git
# Create PKGBUILD there
```

See [docs/PACKAGING.md](../docs/PACKAGING.md) for a complete PKGBUILD example.

## Notes

- This repo does NOT contain distribution-specific packaging files (PKGBUILD, .deb, .rpm, etc.)
- Package maintainers create those in their own repositories
- We only provide generic files that are useful across distributions

---

**Maintaining a package?** Let us know and we'll link to it in the README!