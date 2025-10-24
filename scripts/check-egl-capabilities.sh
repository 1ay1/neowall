#!/bin/bash
# EGL Capability Checker using eglinfo
# Validates your system's EGL/OpenGL ES capabilities

set -e

COLOR_BLUE='\033[1;34m'
COLOR_GREEN='\033[1;32m'
COLOR_YELLOW='\033[1;33m'
COLOR_RED='\033[1;31m'
COLOR_RESET='\033[0m'

echo -e "${COLOR_BLUE}╔══════════════════════════════════════════════════════════════╗${COLOR_RESET}"
echo -e "${COLOR_BLUE}║          EGL/OpenGL ES Capability Detection                ║${COLOR_RESET}"
echo -e "${COLOR_BLUE}╚══════════════════════════════════════════════════════════════╝${COLOR_RESET}"
echo

# Check if eglinfo is available
if ! command -v eglinfo &> /dev/null; then
    echo -e "${COLOR_RED}✗ eglinfo not found${COLOR_RESET}"
    echo "  Install: sudo pacman -S mesa-utils (Arch) or sudo apt install mesa-utils (Ubuntu)"
    exit 1
fi

echo -e "${COLOR_GREEN}✓ eglinfo found${COLOR_RESET}"
echo

# Extract Wayland platform info (most relevant for staticwall)
echo -e "${COLOR_BLUE}=== Wayland Platform Detection ===${COLOR_RESET}"
eglinfo -B 2>/dev/null | sed -n '/^Wayland platform:/,/^$/p' | grep -E "EGL (API|version|vendor)|OpenGL ES (profile|version)" | while read line; do
    if echo "$line" | grep -q "OpenGL ES profile version"; then
        version=$(echo "$line" | grep -oP "OpenGL ES \K[0-9.]+")
        echo -e "${COLOR_GREEN}✓ OpenGL ES Version: $version${COLOR_RESET}"
    elif echo "$line" | grep -q "EGL version"; then
        version=$(echo "$line" | grep -oP "EGL version string: \K[0-9.]+")
        echo -e "${COLOR_GREEN}✓ EGL Version: $version${COLOR_RESET}"
    elif echo "$line" | grep -q "EGL vendor"; then
        vendor=$(echo "$line" | cut -d: -f2-)
        echo -e "${COLOR_BLUE}  Vendor:$vendor${COLOR_RESET}"
    fi
done
echo

# Check what GL ES versions are supported
echo -e "${COLOR_BLUE}=== Detected Capabilities ===${COLOR_RESET}"

GL_VERSION=$(eglinfo -B 2>/dev/null | grep "Wayland platform:" -A 20 | grep "OpenGL ES profile version" | head -1 | grep -oP "OpenGL ES \K[0-9.]+")

if [[ "$GL_VERSION" =~ ^3\.2 ]]; then
    echo -e "${COLOR_GREEN}✓ OpenGL ES 3.2${COLOR_RESET} - Full support (geometry/tessellation shaders)"
    echo -e "${COLOR_GREEN}✓ OpenGL ES 3.1${COLOR_RESET} - Full support (compute shaders)"
    echo -e "${COLOR_GREEN}✓ OpenGL ES 3.0${COLOR_RESET} - Full support (enhanced Shadertoy)"
    echo -e "${COLOR_GREEN}✓ OpenGL ES 2.0${COLOR_RESET} - Full support (baseline)"
elif [[ "$GL_VERSION" =~ ^3\.1 ]]; then
    echo -e "${COLOR_GREEN}✓ OpenGL ES 3.1${COLOR_RESET} - Full support (compute shaders)"
    echo -e "${COLOR_GREEN}✓ OpenGL ES 3.0${COLOR_RESET} - Full support (enhanced Shadertoy)"
    echo -e "${COLOR_GREEN}✓ OpenGL ES 2.0${COLOR_RESET} - Full support (baseline)"
    echo -e "${COLOR_YELLOW}○ OpenGL ES 3.2${COLOR_RESET} - Not available"
elif [[ "$GL_VERSION" =~ ^3\.0 ]]; then
    echo -e "${COLOR_GREEN}✓ OpenGL ES 3.0${COLOR_RESET} - Full support (enhanced Shadertoy)"
    echo -e "${COLOR_GREEN}✓ OpenGL ES 2.0${COLOR_RESET} - Full support (baseline)"
    echo -e "${COLOR_YELLOW}○ OpenGL ES 3.1${COLOR_RESET} - Not available"
    echo -e "${COLOR_YELLOW}○ OpenGL ES 3.2${COLOR_RESET} - Not available"
elif [[ "$GL_VERSION" =~ ^2\.0 ]]; then
    echo -e "${COLOR_GREEN}✓ OpenGL ES 2.0${COLOR_RESET} - Full support (baseline)"
    echo -e "${COLOR_YELLOW}○ OpenGL ES 3.0${COLOR_RESET} - Not available"
    echo -e "${COLOR_YELLOW}○ OpenGL ES 3.1${COLOR_RESET} - Not available"
    echo -e "${COLOR_YELLOW}○ OpenGL ES 3.2${COLOR_RESET} - Not available"
else
    echo -e "${COLOR_RED}✗ Could not detect OpenGL ES version${COLOR_RESET}"
fi
echo

# Check important extensions
echo -e "${COLOR_BLUE}=== Important Extensions ===${COLOR_RESET}"
EXTENSIONS=$(eglinfo -B 2>/dev/null | grep "Wayland platform:" -A 100 | grep "EGL extensions string:" -A 50 | head -50)

check_ext() {
    if echo "$EXTENSIONS" | grep -q "$1"; then
        echo -e "${COLOR_GREEN}✓ $2${COLOR_RESET}"
        return 0
    else
        echo -e "${COLOR_YELLOW}○ $2${COLOR_RESET} - not available"
        return 1
    fi
}

check_ext "EGL_KHR_image" "EGL_KHR_image (zero-copy texturing)"
check_ext "EGL_KHR_fence_sync" "EGL_KHR_fence_sync (GPU synchronization)"
check_ext "EGL_WL_bind_wayland_display" "EGL_WL_bind_wayland_display (Wayland integration)"
check_ext "EGL_KHR_surfaceless_context" "EGL_KHR_surfaceless_context"
echo

# Staticwall compatibility check
echo -e "${COLOR_BLUE}=== Staticwall Compatibility ===${COLOR_RESET}"
if [[ "$GL_VERSION" =~ ^3\. ]]; then
    echo -e "${COLOR_GREEN}✓ Your system supports Staticwall with enhanced features${COLOR_RESET}"
    echo -e "  • Enhanced Shadertoy support (texture(), in/out, integers)"
    echo -e "  • Better shader compatibility"
    if [[ "$GL_VERSION" =~ ^3\.[12] ]]; then
        echo -e "  • Advanced features available (compute/geometry shaders)"
    fi
elif [[ "$GL_VERSION" =~ ^2\.0 ]]; then
    echo -e "${COLOR_YELLOW}⚠ Your system supports Staticwall with basic features${COLOR_RESET}"
    echo -e "  • Basic Shadertoy support (texture2D(), varying)"
    echo -e "  • Some shaders may need adaptation"
else
    echo -e "${COLOR_RED}✗ Your system may not support Staticwall${COLOR_RESET}"
    echo -e "  • OpenGL ES 2.0 or later required"
fi
echo

echo -e "${COLOR_BLUE}╔══════════════════════════════════════════════════════════════╗${COLOR_RESET}"
echo -e "${COLOR_BLUE}║                    Detection Complete                       ║${COLOR_RESET}"
echo -e "${COLOR_BLUE}╚══════════════════════════════════════════════════════════════╝${COLOR_RESET}"
