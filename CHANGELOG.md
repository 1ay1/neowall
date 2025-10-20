# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

*Sets wallpapers until it... doesn't.*

## [Unreleased]

### Added
- Initial release
- Wayland wallpaper support via wlr-layer-shell protocol
- Multi-monitor support with per-output configuration
- Multiple display modes: center, fill, fit, stretch, tile
- Automatic wallpaper cycling with configurable duration
- Directory-based cycling (auto-loads all images from a folder)
- Smooth transition effects: fade, slide left, slide right
- Hot-reload configuration via SIGHUP signal
- Config file watching with `--watch` flag
- VIBE configuration format (human-readable, no quotes needed)
- PNG and JPEG image format support
- EGL/OpenGL ES 2.0 hardware-accelerated rendering
- Daemon mode with `-d` flag
- Verbose logging with `-v` flag
- Default configuration creation on first run
- Default wallpaper asset included

### Changed
- N/A (initial release)

### Deprecated
- N/A

### Removed
- N/A

### Fixed
- N/A

### Security
- N/A

## [0.1.0] - 2024-10-20

### Added
- Initial public release
- Core wallpaper daemon functionality (sets wallpapers until it doesn't)
- Wayland protocol support
- Multi-monitor management
- Image loading and rendering
- Configuration file parsing
- Documentation and examples
- Embraced the irony of "Staticwall" dynamically cycling wallpapers
- Self-aware humor about our naming choices

[Unreleased]: https://github.com/1ay1/staticwall/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/1ay1/staticwall/releases/tag/v0.1.0