# Publishing Staticwall to GitHub

This guide will help you prepare and publish Staticwall to GitHub.

## Pre-Publishing Checklist

### 1. Final Verification

```bash
# Clean build
make clean && make

# Test the binary
./build/bin/staticwall --help
./build/bin/staticwall --version

# Verify it runs (will need Wayland)
./build/bin/staticwall -v

# Check for memory leaks (optional)
valgrind --leak-check=full ./build/bin/staticwall
```

### 2. Update Version Information

If needed, update version in:
- `include/staticwall.h` - `#define STATICWALL_VERSION "0.1.0"`
- `Makefile` - `VERSION = 0.1.0`
- `CHANGELOG.md` - Add release notes

### 3. Replace Placeholder URLs

Search and replace `1ay1` with your actual GitHub username:

```bash
# Find all occurrences
grep -r "1ay1" .

# Replace in README.md
sed -i 's/1ay1/1ay1/g' README.md

# Replace in CHANGELOG.md
sed -i 's/1ay1/1ay1/g' CHANGELOG.md
```

Also update:
- `README.md` - Badge URLs and clone instructions
- `CHANGELOG.md` - Release comparison URLs
- `CONTRIBUTING.md` - Fork and clone instructions

### 4. Review Documentation

- [ ] `README.md` - Main documentation is clear and complete
- [ ] `docs/INSTALL.md` - Installation instructions are accurate
- [ ] `docs/QUICKSTART.md` - Quick start examples work
- [ ] `docs/CONFIG_GUIDE.md` - Configuration reference is up-to-date
- [ ] `CONTRIBUTING.md` - Contribution guidelines are clear
- [ ] `CHANGELOG.md` - Changes are documented
- [ ] `LICENSE` - License is appropriate (MIT by default)

### 5. Default Wallpaper

The default wallpaper is located at `assets/default.png`. You can:

**Option A: Keep the generated gradient**
```bash
# Already created - a simple gray gradient
ls -lh assets/default.png
```

**Option B: Replace with your own**
```bash
# Replace with your preferred default image
cp /path/to/your/wallpaper.png assets/default.png
```

**Option C: Create a new solid color or gradient**
```bash
# Solid color (dark gray)
magick -size 3840x2160 xc:#2e3440 assets/default.png

# Gradient (blue to purple)
magick -size 3840x2160 gradient:'#667eea'-'#764ba2' assets/default.png

# Gradient with blur for smooth effect
magick -size 3840x2160 gradient:'#667eea'-'#764ba2' -blur 0x80 assets/default.png
```

## Publishing Steps

### 1. Initialize Git Repository (if not already done)

```bash
cd staticwall
git init
git add .
git commit -m "Initial commit: Staticwall v0.1.0"
```

### 2. Create GitHub Repository

1. Go to https://github.com/new
2. Repository name: `staticwall`
3. Description: "A lightweight, reliable Wayland wallpaper daemon written in C"
4. Choose: Public (recommended for open source)
5. **DO NOT** initialize with README (we already have one)
6. Click "Create repository"

### 3. Push to GitHub

```bash
# Add remote (replace 1ay1)
git remote add origin https://github.com/1ay1/staticwall.git

# Push to GitHub
git branch -M main
git push -u origin main
```

### 4. Configure Repository Settings

On GitHub, go to Settings:

#### About Section (top right)
- Add description: "A lightweight, reliable Wayland wallpaper daemon written in C"
- Add website (if you have one)
- Add topics: `wayland`, `wallpaper`, `c`, `linux`, `sway`, `hyprland`, `wlroots`

#### Features
- ✅ Issues
- ✅ Discussions (recommended)
- ✅ Projects (optional)
- ✅ Preserve this repository (optional)
- ✅ Sponsorships (if you want to enable GitHub Sponsors)

#### Pull Requests
- ✅ Allow squash merging
- ✅ Allow auto-merge
- ✅ Automatically delete head branches

#### GitHub Pages (optional)
- Source: Deploy from a branch
- Branch: `gh-pages` (if you want to create a website)

### 5. Create First Release

1. Go to "Releases" on the right sidebar
2. Click "Create a new release"
3. Tag version: `v0.1.0`
4. Release title: `Staticwall v0.1.0 - Initial Release`
5. Description: Copy from CHANGELOG.md
6. Click "Publish release"

Example release notes:
```markdown
# Staticwall v0.1.0 - Initial Release

A lightweight, reliable Wayland wallpaper daemon written in C.

## Features

- 🎨 Multiple display modes (center, fill, fit, stretch, tile)
- 🔄 Automatic cycling through wallpapers
- 🖥️ Multi-monitor support with per-output configuration
- ⚡ Hardware-accelerated OpenGL ES rendering
- 🔥 Hot-reload configuration (SIGHUP)
- 🎬 Smooth transition effects
- 📁 Directory-based cycling
- 🔧 Simple VIBE config format

## Installation

See [INSTALL.md](docs/INSTALL.md) for detailed instructions.

### Quick Install (Arch Linux)
```bash
yay -S staticwall-git
```

### Build from Source
```bash
git clone https://github.com/1ay1/staticwall.git
cd staticwall
make
sudo make install
```

## Quick Start

```bash
# Run staticwall (creates default config on first run)
staticwall

# Edit config
$EDITOR ~/.config/staticwall/config.vibe

# Reload configuration
killall -HUP staticwall
```

## Supported Compositors

- Sway, Hyprland, River, Wayfire, and other wlroots-based compositors

## Documentation

- [README.md](README.md) - Main documentation
- [Quick Start Guide](docs/QUICKSTART.md)
- [Configuration Guide](docs/CONFIG_GUIDE.md)
- [Installation Guide](docs/INSTALL.md)

---

**Full Changelog**: https://github.com/1ay1/staticwall/commits/v0.1.0
```

### 6. Add Repository Badges (optional)

Update README.md with additional badges:

```markdown
[![Build](https://github.com/1ay1/staticwall/workflows/Build/badge.svg)](https://github.com/1ay1/staticwall/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![GitHub release](https://img.shields.io/github/v/release/1ay1/staticwall)](https://github.com/1ay1/staticwall/releases)
[![AUR version](https://img.shields.io/aur/version/staticwall-git)](https://aur.archlinux.org/packages/staticwall-git)
```

## Post-Publishing Tasks

### 1. Enable GitHub Actions

The `.github/workflows/build.yml` will automatically run on push. Check:
- Go to "Actions" tab
- Verify build passes on Ubuntu and Arch Linux

### 2. Create AUR Package (Arch Linux users)

Create a PKGBUILD for the AUR:

```bash
# In a separate directory
mkdir staticwall-aur
cd staticwall-aur

# Create PKGBUILD (see example below)
nano PKGBUILD

# Test build
makepkg -si

# Publish to AUR
makepkg --printsrcinfo > .SRCINFO
git init
git remote add origin ssh://aur@aur.archlinux.org/staticwall-git.git
git add PKGBUILD .SRCINFO
git commit -m "Initial commit: staticwall-git"
git push -u origin master
```

Example PKGBUILD:
```bash
# Maintainer: Your Name <your.email@example.com>
pkgname=staticwall-git
pkgver=r1.0.1.0
pkgrel=1
pkgdesc="A lightweight, reliable Wayland wallpaper daemon written in C"
arch=('x86_64')
url="https://github.com/1ay1/staticwall"
license=('MIT')
depends=('wayland' 'egl-wayland' 'mesa' 'libpng' 'libjpeg-turbo')
makedepends=('git' 'wayland-protocols')
provides=('staticwall')
conflicts=('staticwall')
source=("git+$url.git")
sha256sums=('SKIP')

pkgver() {
    cd "$srcdir/staticwall"
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cd "$srcdir/staticwall"
    make
}

package() {
    cd "$srcdir/staticwall"
    make DESTDIR="$pkgdir/" install
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
```

### 3. Announce Your Project

Share on:
- Reddit: r/unixporn, r/swaywm, r/hyprland, r/linux
- Discord: Sway, Hyprland, wlroots communities
- Mastodon: #wayland #linux #opensource
- Hacker News: Show HN (if you want)
- Your personal blog/social media

Example announcement:
```
🎉 Introducing Staticwall - A Lightweight Wayland Wallpaper Daemon

I've built a reliable wallpaper daemon for Wayland (wlroots) compositors, 
written in C with focus on stability and simplicity.

Features:
✅ Multi-monitor support
✅ Automatic cycling
✅ Hot-reload config
✅ Smooth transitions
✅ Hardware accelerated

Works with Sway, Hyprland, River, and other wlroots compositors.

GitHub: https://github.com/1ay1/staticwall
```

### 4. Monitor and Respond

- Watch for issues and PRs
- Respond to questions promptly
- Thank contributors
- Keep CHANGELOG.md updated

### 5. Set Up Project Management (optional)

- Create GitHub Projects for roadmap
- Add issue templates
- Set up discussions
- Create wiki for extended documentation

## Maintenance Tips

### Regular Tasks

1. **Respond to Issues**: Aim for 24-48 hour response time
2. **Review PRs**: Test thoroughly before merging
3. **Update Dependencies**: Check for security updates
4. **Release Schedule**: Consider semantic versioning
5. **Documentation**: Keep docs in sync with code

### Version Bumping

For new releases:

```bash
# Update version
vim include/staticwall.h  # Update STATICWALL_VERSION
vim Makefile              # Update VERSION
vim CHANGELOG.md          # Add new section

# Commit and tag
git add .
git commit -m "Bump version to v0.2.0"
git tag -a v0.2.0 -m "Release version 0.2.0"
git push origin main --tags

# Create GitHub release
# Use the GitHub web interface or gh CLI
```

## Troubleshooting

### Build Fails on CI

- Check GitHub Actions logs
- Verify dependencies are correct
- Test on Ubuntu/Arch locally

### Documentation Out of Date

- Set reminder to review docs quarterly
- Ask contributors to update docs with PRs

### Too Many Issues/PRs

- Create issue templates
- Label issues appropriately
- Close stale issues/PRs
- Consider adding more maintainers

## Resources

- [GitHub Docs](https://docs.github.com/)
- [Semantic Versioning](https://semver.org/)
- [Keep a Changelog](https://keepachangelog.com/)
- [AUR Package Guidelines](https://wiki.archlinux.org/title/AUR_submission_guidelines)
- [Open Source Guide](https://opensource.guide/)

## Support

If you need help publishing:
1. Check GitHub's documentation
2. Ask in GitHub Community Discussions
3. Reach out to experienced open source maintainers

---

Good luck with your open source project! 🚀