#!/bin/bash
# Generate capability detection code from eglinfo -B output

echo "// Auto-generated from eglinfo -B"
echo "// Generated: $(date)"
echo ""

# Extract GL ES version
GL_VERSION=$(eglinfo -B 2>/dev/null | grep "Wayland platform:" -A 20 | grep "OpenGL ES profile version" | head -1 | grep -oP "OpenGL ES \K[0-9]\.[0-9]")

if [ -n "$GL_VERSION" ]; then
    echo "// Detected OpenGL ES: $GL_VERSION"
    
    # Generate version enum
    MAJOR=$(echo $GL_VERSION | cut -d. -f1)
    MINOR=$(echo $GL_VERSION | cut -d. -f2)
    
    echo ""
    echo "static const int detected_gles_major = $MAJOR;"
    echo "static const int detected_gles_minor = $MINOR;"
    echo ""
    
    # Generate version check function
    cat << 'FUNC'
static inline bool system_supports_gles_version(int major, int minor) {
    return (detected_gles_major > major) || 
           (detected_gles_major == major && detected_gles_minor >= minor);
}
FUNC
fi

echo ""
echo "// Extension support flags"

# Extract extensions
eglinfo -B 2>/dev/null | grep "Wayland platform:" -A 100 | grep "EGL extensions string:" -A 50 | grep -oE "EGL_[A-Za-z0-9_]+" | sort -u | while read ext; do
    # Convert extension name to C identifier
    c_name=$(echo "$ext" | tr '[:upper:]' '[:lower:]' | tr '/' '_')
    echo "#define HAVE_${ext} 1  // ${c_name}"
done
