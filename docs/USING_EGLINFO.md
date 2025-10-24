# Using `eglinfo -B` with Staticwall

This guide shows how to use `eglinfo -B` to make development and testing easier.

## Quick Reference

### Check Your System Capabilities

```bash
# Quick check for Wayland (what Staticwall uses)
eglinfo -B 2>/dev/null | grep -A 20 "Wayland platform:"

# Or use our script
./scripts/egl-check.sh
```

### What You'll See

```
Wayland platform:
EGL API version: 1.5
EGL vendor string: NVIDIA
EGL version string: 1.5
EGL client APIs: OpenGL_ES OpenGL
OpenGL ES profile version: OpenGL ES 3.2 NVIDIA 580.95.05
```

This tells you:
- **EGL 1.5** - Latest EGL version
- **OpenGL ES 3.2** - Highest GL ES version available
- **NVIDIA** - Your GPU vendor

---

## Use Cases

### 1. Pre-Build Validation

**Before building Staticwall**, check your system capabilities:

```bash
GL_VERSION=$(eglinfo -B 2>/dev/null | grep "Wayland.*OpenGL ES profile version" -A 5 | grep "OpenGL ES" | head -1 | grep -oP "OpenGL ES \K[0-9.]+")
echo "Your system supports: OpenGL ES $GL_VERSION"
```

Expected output:
```
Your system supports: OpenGL ES 3.2
```

### 2. Debugging Build Issues

If the Makefile can't detect your GL version:

```bash
# Check what eglinfo sees
eglinfo -B 2>/dev/null | grep "Wayland" -A 20

# Check pkg-config
pkg-config --exists glesv2 && echo "✓ GLESv2 found"
pkg-config --modversion glesv2
```

### 3. Testing on Different Systems

**Before deploying** to a new system:

```bash
ssh user@remote-system "eglinfo -B 2>/dev/null | grep -E '(Wayland|OpenGL ES profile version)'"
```

This shows what GL ES version the remote system has.

### 4. Validating Extension Support

Check if specific extensions are available:

```bash
# Check for important extensions
eglinfo -B 2>/dev/null | grep "Wayland" -A 100 | grep "EGL extensions" -A 50 | grep -E "(EGL_KHR_image|EGL_KHR_fence_sync|EGL_WL_bind)"
```

Look for:
- `EGL_KHR_image` - Zero-copy texturing
- `EGL_KHR_fence_sync` - GPU synchronization  
- `EGL_WL_bind_wayland_display` - Wayland integration

---

## Integration with Makefile

The Makefile already does similar detection, but you can enhance it:

### Option 1: Automatic Detection Fallback

Add to Makefile after line 50:

```makefile
# Fallback: Use eglinfo if pkg-config fails
ifeq ($(HAVE_GLES3),)
    EGLINFO_VERSION := $(shell eglinfo -B 2>/dev/null | grep "Wayland" -A 10 | grep "OpenGL ES profile version" | grep -oP "OpenGL ES \K[0-9]\.[0-9]" | head -1)
    ifneq ($(EGLINFO_VERSION),)
        ifeq ($(shell echo "$(EGLINFO_VERSION) >= 3.2" | bc),1)
            HAVE_GLES32 := 1
            HAVE_GLES31 := 1
            HAVE_GLES3 := 1
        else ifeq ($(shell echo "$(EGLINFO_VERSION) >= 3.1" | bc),1)
            HAVE_GLES31 := 1
            HAVE_GLES3 := 1
        else ifeq ($(shell echo "$(EGLINFO_VERSION) >= 3.0" | bc),1)
            HAVE_GLES3 := 1
        endif
    endif
endif
```

### Option 2: Runtime Comparison

Compare build-time detection vs runtime:

```bash
make print-caps > /tmp/build-caps.txt
eglinfo -B 2>/dev/null | grep "Wayland" -A 10 > /tmp/runtime-caps.txt
diff /tmp/build-caps.txt /tmp/runtime-caps.txt
```

---

## Troubleshooting

### "eglinfo: command not found"

Install mesa-utils:

```bash
# Arch Linux
sudo pacman -S mesa-utils

# Ubuntu/Debian  
sudo apt install mesa-utils

# Fedora
sudo dnf install mesa-demos
```

### "eglInitialize failed"

This usually means:
1. No GPU available
2. No display server running
3. Wrong platform selected

Try:
```bash
EGL_PLATFORM=wayland eglinfo -B
```

### Different Version at Build vs Runtime

If `make print-caps` shows ES 3.2 but `eglinfo` shows ES 2.0:

1. Check you're on the same display:
   ```bash
   echo $WAYLAND_DISPLAY
   echo $DISPLAY
   ```

2. Check you're using the same GPU:
   ```bash
   eglinfo -B 2>/dev/null | grep "vendor"
   ```

3. Check pkg-config vs eglinfo:
   ```bash
   pkg-config --modversion glesv2
   eglinfo -B | grep "OpenGL ES profile version"
   ```

---

## Scripts

We've provided helper scripts in `scripts/`:

### scripts/egl-check.sh

Quick capability check:

```bash
./scripts/egl-check.sh
```

Shows:
- EGL version
- OpenGL ES version
- Vendor/Renderer

### scripts/check-egl-capabilities.sh

Comprehensive check with Staticwall compatibility analysis:

```bash
./scripts/check-egl-capabilities.sh
```

Shows:
- All supported GL ES versions
- Important extensions
- Staticwall compatibility status
- Recommended features

---

## Comparison: Build Detection vs eglinfo

| Method | When | What It Detects | Use Case |
|--------|------|-----------------|----------|
| **Makefile (pkg-config)** | Build time | Installed headers/libs | What you can *compile* against |
| **eglinfo -B** | Runtime | Actual GPU capabilities | What will *run* on system |
| **Staticwall detection** | Runtime | Active GL context | What *is being used* |

**Best Practice**: All three should agree!

```bash
# 1. Build-time
make print-caps

# 2. System runtime  
eglinfo -B | grep "Wayland" -A 10

# 3. Staticwall runtime
./build/bin/staticwall -f -v 2>&1 | grep "OpenGL ES"
```

Expected (all should match):
```
✓ OpenGL ES 3.2
  OpenGL ES profile version: OpenGL ES 3.2
  Created OpenGL ES 3.0 context (enhanced Shadertoy support)
```

---

## Advanced: Automated Testing

### CI/CD Integration

```yaml
# .github/workflows/build.yml
- name: Check System Capabilities
  run: |
    eglinfo -B || echo "No GPU, using software rendering"
    
- name: Validate Build Detection
  run: |
    make print-caps
    # Fail if no GL ES support detected
    make print-caps | grep -q "OpenGL ES" || exit 1
```

### Pre-commit Hook

Create `.git/hooks/pre-commit`:

```bash
#!/bin/bash
# Validate EGL capabilities before commit

if command -v eglinfo &> /dev/null; then
    GL_VERSION=$(eglinfo -B 2>/dev/null | grep "Wayland" -A 10 | grep "OpenGL ES" | head -1 | grep -oP "[0-9]\.[0-9]")
    if [ -n "$GL_VERSION" ]; then
        echo "✓ System supports OpenGL ES $GL_VERSION"
    fi
fi
```

---

## Summary

### Quick Commands

```bash
# Check your system
eglinfo -B 2>/dev/null | grep -A 10 "Wayland"

# Use our script
./scripts/egl-check.sh

# Compare with build
make print-caps && eglinfo -B | grep "OpenGL ES"

# Test Staticwall
./build/bin/staticwall -f -v 2>&1 | grep -E "(Created|Using) OpenGL"
```

### When to Use What

- **Development**: Use `make print-caps` to see what you're building against
- **Debugging**: Use `eglinfo -B` to see what the system actually has
- **Deployment**: Use both to ensure compatibility
- **Runtime**: Check Staticwall's logs to see what it actually uses

---

**TL;DR**: Run `eglinfo -B 2>/dev/null | grep "Wayland" -A 10` to quickly see your system's OpenGL ES capabilities.
