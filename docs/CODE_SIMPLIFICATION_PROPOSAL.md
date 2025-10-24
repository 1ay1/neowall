# Code Simplification Using eglinfo -B

## Overview

Current detection code is verbose and manual. We can simplify it significantly.

## Current State vs Simplified

### Current: 573 lines of manual detection

```c
// src/egl/capability.c - Current
gles_version_t gles_detect_version(EGLDisplay display, EGLContext context) {
    // 100+ lines of manual string parsing
    const GLubyte *version_str = glGetString(GL_VERSION);
    if (!version_str) return GLES_VERSION_UNKNOWN;
    
    // Parse "OpenGL ES 3.2 NVIDIA..."
    if (strstr((const char*)version_str, "OpenGL ES 3.2")) {
        return GLES_VERSION_3_2;
    } else if (strstr((const char*)version_str, "OpenGL ES 3.1")) {
        return GLES_VERSION_3_1;
    }
    // ... 50 more lines
}
```

### Simplified: ~50 lines with regex

```c
// Simplified version
static gles_version_t parse_gles_version_string(const char *version_str) {
    regex_t regex;
    regmatch_t matches[3];
    
    // Match "OpenGL ES X.Y"
    if (regcomp(&regex, "OpenGL ES ([0-9])\\.([0-9])", REG_EXTENDED) != 0) {
        return GLES_VERSION_UNKNOWN;
    }
    
    if (regexec(&regex, version_str, 3, matches, 0) == 0) {
        int major = version_str[matches[1].rm_so] - '0';
        int minor = version_str[matches[2].rm_so] - '0';
        regfree(&regex);
        
        return (major * 10) + minor;  // 3.2 -> 32
    }
    
    regfree(&regex);
    return GLES_VERSION_UNKNOWN;
}

gles_version_t gles_detect_version(EGLDisplay display, EGLContext context) {
    const GLubyte *version_str = glGetString(GL_VERSION);
    return version_str ? parse_gles_version_string((const char*)version_str) 
                       : GLES_VERSION_UNKNOWN;
}
```

**Lines saved: ~100 lines**

---

## 2. **Simplify Extension Detection** (Moderate Win)

### Current: Manual string searching

```c
// Current: Repeated for 50+ extensions
caps->has_egl_khr_image_base = false;
if (egl_extensions) {
    if (strstr(egl_extensions, "EGL_KHR_image_base")) {
        caps->has_egl_khr_image_base = true;
    }
}
// Repeat 50 times...
```

### Simplified: Table-driven

```c
// Simplified
static const struct {
    const char *name;
    size_t offset;  // offset in caps struct
} extension_table[] = {
    {"EGL_KHR_image_base", offsetof(egl_capabilities_t, has_egl_khr_image_base)},
    {"EGL_KHR_fence_sync", offsetof(egl_capabilities_t, has_egl_khr_fence_sync)},
    // ... just data, no code
};

void detect_extensions(egl_capabilities_t *caps, const char *ext_string) {
    for (size_t i = 0; i < ARRAY_SIZE(extension_table); i++) {
        bool *flag = (bool*)((char*)caps + extension_table[i].offset);
        *flag = (ext_string && strstr(ext_string, extension_table[i].name) != NULL);
    }
}
```

**Lines saved: ~200 lines**

---

## 3. **Use eglinfo -B as Fallback** (Safety Net)

### Add Fallback Detection

```c
// In capability.c
bool egl_detect_capabilities_fallback(egl_capabilities_t *caps) {
    // If runtime detection fails, try eglinfo
    FILE *fp = popen("eglinfo -B 2>/dev/null | grep 'Wayland' -A 20", "r");
    if (!fp) return false;
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "OpenGL ES profile version")) {
            // Parse version from eglinfo output
            float version = 0;
            sscanf(line, "%*[^0-9]%f", &version);
            if (version >= 3.2) caps->gles_version = GLES_VERSION_3_2;
            else if (version >= 3.1) caps->gles_version = GLES_VERSION_3_1;
            else if (version >= 3.0) caps->gles_version = GLES_VERSION_3_0;
            else caps->gles_version = GLES_VERSION_2_0;
        }
    }
    
    pclose(fp);
    return caps->gles_version != GLES_VERSION_UNKNOWN;
}

bool egl_detect_capabilities(EGLDisplay display, egl_capabilities_t *caps) {
    // Try runtime detection first
    if (detect_capabilities_runtime(display, caps)) {
        return true;
    }
    
    // Fallback to eglinfo
    log_warn("Runtime detection failed, using eglinfo fallback");
    return egl_detect_capabilities_fallback(caps);
}
```

**Benefit: Never fails completely**

---

## 4. **Generate Makefile Detection from eglinfo** (Build Time)

### Instead of pkg-config, use eglinfo

```makefile
# Current Makefile (lines 50-80): Manual pkg-config checks
HAVE_GLES3 := $(shell pkg-config --exists glesv2 && echo 1)
HAVE_GLES31 := $(shell pkg-config --exists glesv2 && echo 1)
# etc... lots of guessing

# Simplified: Use eglinfo directly
EGLINFO_GLES_VERSION := $(shell eglinfo -B 2>/dev/null | grep "Wayland" -A 10 | grep "OpenGL ES profile version" | grep -oP "[0-9]\.[0-9]" | head -1)

# Auto-detect everything from actual GPU
ifeq ($(shell echo "$(EGLINFO_GLES_VERSION) >= 3.2" | bc 2>/dev/null),1)
    HAVE_GLES32 := 1
    HAVE_GLES31 := 1
    HAVE_GLES3 := 1
    HAVE_GLES2 := 1
else ifeq ($(shell echo "$(EGLINFO_GLES_VERSION) >= 3.1" | bc 2>/dev/null),1)
    HAVE_GLES31 := 1
    HAVE_GLES3 := 1
    HAVE_GLES2 := 1
else ifeq ($(shell echo "$(EGLINFO_GLES_VERSION) >= 3.0" | bc 2>/dev/null),1)
    HAVE_GLES3 := 1
    HAVE_GLES2 := 1
else
    HAVE_GLES2 := 1
endif
```

**Lines saved: ~30 lines, more accurate**

---

## 5. **Auto-generate Test Fixtures**

```bash
# Generate test data from real system
./scripts/generate-test-fixtures.sh > tests/fixtures/nvidia-rtx4060.h
```

```c
// tests/fixtures/nvidia-rtx4060.h - Auto-generated
static const char *test_egl_version = "1.5";
static const char *test_gles_version = "OpenGL ES 3.2 NVIDIA 580.95.05";
static const char *test_vendor = "NVIDIA";
static const char *test_extensions[] = {
    "EGL_KHR_image_base",
    "EGL_KHR_fence_sync",
    // ... all extensions from eglinfo
    NULL
};

// Use in tests
TEST(capability_detection) {
    egl_capabilities_t caps;
    parse_version_string(test_gles_version, &caps);
    ASSERT_EQ(caps.gles_version, GLES_VERSION_3_2);
}
```

**Benefit: Tests based on real hardware**

---

## Recommended Simplifications (Priority Order)

### Priority 1: Simplify Extension Detection ⭐⭐⭐
- **Impact**: High (200 lines saved)
- **Risk**: Low (table-driven is standard)
- **Effort**: 1-2 hours

### Priority 2: Simplify Version Parsing ⭐⭐
- **Impact**: Medium (100 lines saved)
- **Risk**: Low (regex is well-tested)
- **Effort**: 1 hour

### Priority 3: Add eglinfo Fallback ⭐⭐
- **Impact**: Medium (reliability improvement)
- **Risk**: Low (fallback only)
- **Effort**: 30 minutes

### Priority 4: Makefile Simplification ⭐
- **Impact**: Low (30 lines saved)
- **Risk**: Medium (build-time dependency on eglinfo)
- **Effort**: 30 minutes

---

## Implementation Plan

### Phase 1: Extension Detection (Safe, High Impact)

1. Create extension table in `capability.c`
2. Replace manual checks with table lookup
3. Test on your system
4. Commit

**Estimated time**: 2 hours  
**Lines saved**: ~200

### Phase 2: Version Parsing (Medium Impact)

1. Add regex-based parser
2. Keep old parser as fallback
3. Test both paths
4. Commit

**Estimated time**: 1 hour  
**Lines saved**: ~100

### Phase 3: eglinfo Fallback (Safety Net)

1. Add `egl_detect_capabilities_fallback()`
2. Call only if runtime fails
3. Add logging
4. Test by forcing runtime failure
5. Commit

**Estimated time**: 30 minutes  
**Lines saved**: 0 (adds ~50 lines, but adds reliability)

---

## Total Potential Savings

- **Lines removed**: ~300-400
- **Complexity reduced**: ~40%
- **Reliability improved**: +fallback mechanism
- **Maintenance burden**: Significantly reduced

---

## Example: Complete Simplified Detection

```c
// capability_simple.c - Proposed simplified version
// ~150 lines instead of 573

#include <regex.h>

// Extension table (data, not code)
static const struct ext_map {
    const char *name;
    size_t offset;
} extensions[] = {
    #define EXT(name, field) {name, offsetof(egl_capabilities_t, field)}
    EXT("EGL_KHR_image_base", has_egl_khr_image_base),
    EXT("EGL_KHR_fence_sync", has_egl_khr_fence_sync),
    // ... rest of extensions
    #undef EXT
};

// Version parser (regex)
static gles_version_t parse_gles_version(const char *str) {
    regex_t re;
    regmatch_t m[3];
    
    if (regcomp(&re, "OpenGL ES ([0-9])\\.([0-9])", REG_EXTENDED)) 
        return GLES_VERSION_UNKNOWN;
    
    if (regexec(&re, str, 3, m, 0) == 0) {
        int maj = str[m[1].rm_so] - '0';
        int min = str[m[2].rm_so] - '0';
        regfree(&re);
        return (maj * 10) + min;
    }
    
    regfree(&re);
    return GLES_VERSION_UNKNOWN;
}

// Extension detector (table-driven)
static void detect_extensions(egl_capabilities_t *caps, const char *ext_str) {
    for (size_t i = 0; i < ARRAY_SIZE(extensions); i++) {
        bool *flag = (bool*)((char*)caps + extensions[i].offset);
        *flag = ext_str && strstr(ext_str, extensions[i].name);
    }
}

// Main detection (with fallback)
bool egl_detect_capabilities(EGLDisplay display, egl_capabilities_t *caps) {
    // Try runtime
    const GLubyte *ver = glGetString(GL_VERSION);
    if (ver) {
        caps->gles_version = parse_gles_version((const char*)ver);
        const GLubyte *ext = glGetString(GL_EXTENSIONS);
        detect_extensions(caps, (const char*)ext);
        return true;
    }
    
    // Fallback to eglinfo
    return egl_detect_via_eglinfo(caps);
}
```

**Total: ~150 lines vs 573 lines = 74% reduction**

---

## Questions to Consider

1. **Do you want to keep backward compatibility?**
   - If yes: Keep old code, add new simplified code as option
   - If no: Replace directly

2. **Is regex dependency acceptable?**
   - Pro: Standard POSIX regex, available everywhere
   - Con: Adds ~10KB to binary
   - Alternative: Keep simple string parsing

3. **Should eglinfo be required at build time?**
   - Pro: Most accurate detection
   - Con: Build-time dependency
   - Recommendation: Use as fallback only

---

## Recommendation

**Start with Priority 1 (Extension Detection)** - highest impact, lowest risk.

```bash
# Create new branch for simplification
git checkout -b feature/simplify-detection

# Implement table-driven extensions
# Test thoroughly
# Commit

# Then move to Priority 2, etc.
```

Would you like me to implement any of these simplifications?
