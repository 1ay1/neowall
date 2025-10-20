# Contributing to Staticwall

Thank you for your interest in contributing to Staticwall! This document provides guidelines and information for contributors.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Making Changes](#making-changes)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)
- [Code Style](#code-style)
- [Commit Messages](#commit-messages)

## Code of Conduct

- Be respectful and inclusive
- Focus on constructive feedback
- Help others learn and grow
- Keep discussions on-topic and professional

## Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/1ay1/staticwall.git
   cd staticwall
   ```
3. **Add upstream remote**:
   ```bash
   git remote add upstream https://github.com/1ay1/staticwall.git
   ```

## Development Setup

### Install Dependencies

**Arch Linux:**
```bash
sudo pacman -S base-devel wayland wayland-protocols egl-wayland mesa libpng libjpeg-turbo
```

**Debian/Ubuntu:**
```bash
sudo apt install build-essential libwayland-dev libegl-dev libgles2-mesa-dev libpng-dev libjpeg-dev wayland-protocols
```

### Build the Project

```bash
make clean
make debug  # Build with debug symbols
```

### Run Tests

```bash
# Run with verbose logging
./build/bin/staticwall -v

# Test with custom config
./build/bin/staticwall -c test-config.vibe -v
```

## Making Changes

### Branch Naming

Use descriptive branch names:
- `feature/add-webp-support`
- `fix/memory-leak-in-image-loader`
- `docs/improve-installation-guide`
- `refactor/simplify-config-parser`

### Types of Contributions

#### Bug Fixes
1. Create an issue describing the bug (if one doesn't exist)
2. Reference the issue in your PR
3. Include steps to reproduce
4. Add tests if applicable

#### New Features
1. Open an issue to discuss the feature first
2. Wait for maintainer approval before implementing
3. Keep changes focused and incremental
4. Update documentation

#### Documentation
- Fix typos and improve clarity
- Add examples and use cases
- Update guides for new features
- Translate documentation (if applicable)

## Testing

### Manual Testing Checklist

Before submitting a PR, test:

- [ ] Single wallpaper on single monitor
- [ ] Multiple monitors with different wallpapers
- [ ] Wallpaper cycling with directory
- [ ] Config hot-reload (SIGHUP)
- [ ] All display modes (center, fill, fit, stretch, tile)
- [ ] Transition effects
- [ ] Error handling (invalid paths, missing files)
- [ ] Resource cleanup (no memory leaks with valgrind)

### Testing on Different Compositors

If possible, test on:
- Sway
- Hyprland
- River
- Other wlroots-based compositors

### Memory Leak Testing

```bash
# Build with debug symbols
make debug

# Run with valgrind
valgrind --leak-check=full --show-leak-kinds=all ./build/bin/staticwall -v

# Let it run for a few cycles, then stop with Ctrl+C
# Check for memory leaks in the output
```

## Submitting Changes

### Pull Request Process

1. **Update your fork**:
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

2. **Create a feature branch**:
   ```bash
   git checkout -b feature/my-new-feature
   ```

3. **Make your changes** and commit them

4. **Push to your fork**:
   ```bash
   git push origin feature/my-new-feature
   ```

5. **Open a Pull Request** on GitHub

### PR Guidelines

- Write a clear title and description
- Reference related issues (e.g., "Fixes #123")
- Include screenshots/recordings for UI changes
- Update documentation if needed
- Ensure code compiles without warnings
- Test thoroughly before submitting

### PR Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Refactoring
- [ ] Other (please describe)

## Testing
- [ ] Tested on [compositor name]
- [ ] No memory leaks (valgrind)
- [ ] Config hot-reload works
- [ ] All display modes work

## Related Issues
Fixes #issue_number

## Screenshots (if applicable)
```

## Code Style

### General Guidelines

- **Language**: C11 standard
- **Indentation**: 4 spaces (no tabs)
- **Line length**: Max 100 characters preferred, 120 hard limit
- **Braces**: K&R style (opening brace on same line)
- **Comments**: Use `/* */` for multi-line, `//` for single-line

### Naming Conventions

```c
// Functions: lowercase with underscores
void function_name(int parameter);

// Structs: lowercase with underscores
struct output_state {
    int width;
    int height;
};

// Enums: UPPERCASE with underscores
enum wallpaper_mode {
    MODE_CENTER,
    MODE_FILL,
};

// Constants/Macros: UPPERCASE with underscores
#define MAX_PATH_LENGTH 4096

// Global variables: avoid if possible, use state struct
```

### Example Code Style

```c
/* Function documentation comment */
bool example_function(struct state *state, const char *path) {
    if (!state || !path) {
        log_error("Invalid parameters");
        return false;
    }
    
    /* Local variables */
    int result = 0;
    char buffer[256];
    
    /* Clear initialization */
    memset(buffer, 0, sizeof(buffer));
    
    /* Check conditions */
    if (state->running) {
        result = do_something(state, buffer);
        if (result < 0) {
            log_error("Operation failed: %d", result);
            return false;
        }
    }
    
    return true;
}
```

### Error Handling

- Always check return values
- Use descriptive error messages
- Clean up resources on error paths
- Log errors with context

```c
FILE *fp = fopen(path, "r");
if (!fp) {
    log_error("Failed to open file %s: %s", path, strerror(errno));
    return false;
}

// ... use file ...

fclose(fp);
```

### Memory Management

- Free all allocated memory
- Set pointers to NULL after freeing
- Use RAII-style cleanup where possible
- Check allocations for NULL

```c
char *buffer = malloc(size);
if (!buffer) {
    log_error("Memory allocation failed");
    return false;
}

// Use buffer...

free(buffer);
buffer = NULL;
```

## Commit Messages

### Format

```
type(scope): short description

Longer explanation if needed. Wrap at 72 characters.

- Bullet points for multiple changes
- Reference issues: Fixes #123

Co-authored-by: Name <email@example.com>
```

### Types

- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style changes (formatting, no logic change)
- `refactor`: Code refactoring
- `perf`: Performance improvements
- `test`: Adding or updating tests
- `chore`: Build system, dependencies, etc.

### Examples

```
feat(cycling): add support for directory-based cycling

Automatically detect when path points to a directory and load all
images for cycling. This eliminates the need to manually list each
image in the config file.

Fixes #45
```

```
fix(egl): prevent context loss on output disconnect

The EGL context was being destroyed when an output was disconnected,
causing the daemon to crash. Now properly handle output removal and
recreate context only when necessary.

Fixes #67
```

## Documentation

When adding features, update:

- `README.md` - Main project overview
- `docs/CONFIG_GUIDE.md` - Configuration options
- `docs/QUICKSTART.md` - Quick start examples
- Code comments - Explain complex logic
- Example config - Add example usage

## Questions?

- Open an issue for bug reports or feature requests
- Use GitHub Discussions for questions and general discussion
- Check existing issues and PRs before creating new ones

## License

By contributing to Staticwall, you agree that your contributions will be licensed under the MIT License.

Thank you for contributing! 🎉