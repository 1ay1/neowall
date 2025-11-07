# Compositor Abstraction - Build Integration Guide

This document provides step-by-step instructions for integrating the compositor abstraction layer into NeoWall's build system.

## 📋 Integration Checklist

### 1. Update Makefile

Add compositor abstraction sources to the build:

```makefile
# Compositor abstraction layer sources
COMPOSITOR_SRCS = \
    $(SRC_DIR)/compositor/compositor_registry.c \
    $(SRC_DIR)/compositor/compositor_surface.c

# Compositor backend sources
COMPOSITOR_BACKEND_SRCS = \
    $(SRC_DIR)/compositor/backends/wlr_layer_shell.c \
    $(SRC_DIR)/compositor/backends/kde_plasma.c \
    $(SRC_DIR)/compositor/backends/gnome_shell.c \
    $(SRC_DIR)/compositor/backends/fallback.c

# Add to main source list
SRCS += $(COMPOSITOR_SRCS) $(COMPOSITOR_BACKEND_SRCS)

# Add compositor include directory
CFLAGS += -I$(INC_DIR)/compositor
```

### 2. Create Object File Directories

Update the Makefile to create compositor object directories:

```makefile
# Create build directories
$(shell mkdir -p $(OBJ_DIR)/compositor)
$(shell mkdir -p $(OBJ_DIR)/compositor/backends)

# Object files
COMPOSITOR_OBJS = $(COMPOSITOR_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
COMPOSITOR_BACKEND_OBJS = $(COMPOSITOR_BACKEND_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

OBJS += $(COMPOSITOR_OBJS) $(COMPOSITOR_BACKEND_OBJS)
```

### 3. Add Build Rules

```makefile
# Compositor source compilation
$(OBJ_DIR)/compositor/%.o: $(SRC_DIR)/compositor/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compositor backend compilation
$(OBJ_DIR)/compositor/backends/%.o: $(SRC_DIR)/compositor/backends/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
```

### 4. Dependencies

The compositor abstraction layer requires:

**Existing dependencies (already in NeoWall):**
- `wayland-client`
- `wayland-egl`
- `EGL`
- `GLES2`

**Protocol dependencies:**
- `wlr-layer-shell-unstable-v1` (already included in protocols/)

**No new dependencies needed!**

### 5. Protocol Files

Ensure wlr-layer-shell protocol is available:

```makefile
# Protocol directory (already exists)
PROTO_DIR = protocols

# wlr-layer-shell protocol (already built)
WLR_LAYER_SHELL_PROTOCOL = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol

# Include in build
CFLAGS += -I$(PROTO_DIR)
LDFLAGS += $(WLR_LAYER_SHELL_PROTOCOL).o
```

### 6. Clean Targets

Update clean target to remove compositor objects:

```makefile
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(OBJ_DIR)/compositor
```

## 🔨 Complete Makefile Changes

Here's a complete diff-style view of changes needed:

```diff
# NeoWall Makefile
PROJECT = neowall
VERSION = 0.3.0

# Directories
SRC_DIR = src
+COMPOSITOR_DIR = $(SRC_DIR)/compositor
+COMPOSITOR_BACKEND_DIR = $(COMPOSITOR_DIR)/backends
INC_DIR = include
PROTO_DIR = protocols
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Create build directories
-$(shell mkdir -p $(OBJ_DIR))
+$(shell mkdir -p $(OBJ_DIR) $(OBJ_DIR)/compositor $(OBJ_DIR)/compositor/backends)

# Source files
SRCS = \
    $(SRC_DIR)/config.c \
    $(SRC_DIR)/eventloop.c \
    $(SRC_DIR)/image.c \
    $(SRC_DIR)/main.c \
    $(SRC_DIR)/output.c \
    $(SRC_DIR)/render.c \
    $(SRC_DIR)/shader.c \
    $(SRC_DIR)/shader_adaptation.c \
    $(SRC_DIR)/shadertoy_compat.c \
    $(SRC_DIR)/utils.c \
    $(SRC_DIR)/vibe.c \
-    $(SRC_DIR)/wayland.c
+    $(SRC_DIR)/wayland.c \
+    $(COMPOSITOR_DIR)/compositor_registry.c \
+    $(COMPOSITOR_DIR)/compositor_surface.c \
+    $(COMPOSITOR_BACKEND_DIR)/wlr_layer_shell.c \
+    $(COMPOSITOR_BACKEND_DIR)/kde_plasma.c \
+    $(COMPOSITOR_BACKEND_DIR)/gnome_shell.c \
+    $(COMPOSITOR_BACKEND_DIR)/fallback.c

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L
-CFLAGS += -I$(INC_DIR) -I$(PROTO_DIR)
+CFLAGS += -I$(INC_DIR) -I$(PROTO_DIR) -I$(INC_DIR)/compositor

# Build rules
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

+# Compositor build rules
+$(OBJ_DIR)/compositor/%.o: $(COMPOSITOR_DIR)/%.c
+	@mkdir -p $(dir $@)
+	$(CC) $(CFLAGS) -c $< -o $@
+
+$(OBJ_DIR)/compositor/backends/%.o: $(COMPOSITOR_BACKEND_DIR)/%.c
+	@mkdir -p $(dir $@)
+	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR)
```

## 🧪 Verify Build

### Step 1: Clean Build

```bash
make clean
```

### Step 2: Build with Verbose Output

```bash
make V=1
```

Expected output should include:
```
gcc -c src/compositor/compositor_registry.c -o build/obj/compositor/compositor_registry.o
gcc -c src/compositor/compositor_surface.c -o build/obj/compositor/compositor_surface.o
gcc -c src/compositor/backends/wlr_layer_shell.c -o build/obj/compositor/backends/wlr_layer_shell.o
gcc -c src/compositor/backends/kde_plasma.c -o build/obj/compositor/backends/kde_plasma.o
gcc -c src/compositor/backends/gnome_shell.c -o build/obj/compositor/backends/gnome_shell.o
gcc -c src/compositor/backends/fallback.c -o build/obj/compositor/backends/fallback.o
```

### Step 3: Check Binary Size

```bash
ls -lh build/bin/neowall
```

Binary should increase by ~50-100KB due to new code.

### Step 4: Verify Symbols

```bash
nm build/bin/neowall | grep compositor_backend_init
```

Should show:
```
00000000004xxxxx T compositor_backend_init
```

### Step 5: Test Run

```bash
./build/bin/neowall -fv
```

Expected output:
```
[INFO] Detected compositor: Hyprland
[INFO] Layer shell support: yes
[DEBUG] Registering available backends...
[DEBUG] Registered backend: wlr-layer-shell (priority: 100)
[DEBUG] Registered backend: kde-plasma (priority: 90)
[DEBUG] Registered backend: gnome-shell (priority: 80)
[DEBUG] Registered backend: fallback (priority: 10)
[INFO] Selected backend: wlr-layer-shell
[INFO] Using backend: wlr-layer-shell - wlroots layer shell protocol
```

## 🐛 Troubleshooting

### Error: `compositor/compositor.h: No such file or directory`

**Fix:** Ensure include path is correct:
```makefile
CFLAGS += -I$(INC_DIR)
```

The file is at `include/compositor/compositor.h`, so `-I$(INC_DIR)` allows `#include "compositor/compositor.h"`.

### Error: `undefined reference to 'compositor_backend_init'`

**Fix:** Ensure compositor sources are in SRCS and being compiled.

Check:
```bash
ls build/obj/compositor/compositor_registry.o
```

### Error: `wlr-layer-shell-unstable-v1-client-protocol.h: No such file`

**Fix:** This is already in protocols/ directory. Ensure it's included:
```makefile
CFLAGS += -I$(PROTO_DIR)
```

### Warning: `unused variable 'state'`

This is normal for stub implementations (KDE/GNOME backends). Can be suppressed with:
```c
(void)state;  // Already in stub code
```

### Build is slow

**Optimization:** Use parallel build:
```bash
make -j$(nproc)
```

## 📦 Installation

No changes needed to install targets:

```makefile
install: $(BIN_DIR)/$(PROJECT)
	install -Dm755 $(BIN_DIR)/$(PROJECT) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	# Compositor abstraction is built into binary
```

## 🔄 Migration from Old Code

### Option 1: Clean Integration (Recommended)

Keep old code while testing new abstraction:

1. Don't remove old wlr-layer-shell code yet
2. Add compositor abstraction alongside
3. Test thoroughly
4. Switch over when confident
5. Remove old code

### Option 2: Direct Replacement

Replace wlr-layer-shell code immediately:

1. Update includes
2. Update function calls
3. Test on wlroots compositor
4. Test fallback behavior

See `INTEGRATION_EXAMPLE.md` for code-level migration guide.

## ✅ Final Checklist

- [ ] Makefile updated with compositor sources
- [ ] Object directories created
- [ ] Build rules added
- [ ] Clean build successful (`make clean && make`)
- [ ] All backends compile without errors
- [ ] Binary runs and detects compositor
- [ ] Correct backend selected for compositor
- [ ] Wallpaper displays correctly
- [ ] Multi-monitor works
- [ ] Hot-reload still works
- [ ] No regression in existing features

## 📝 Notes

### Incremental Build

After initial integration, only modified files rebuild:

```bash
# Modify compositor_registry.c
touch src/compositor/compositor_registry.c

# Only recompiles that file and relinks
make
```

### Debug Builds

Add debug symbols for development:

```bash
make DEBUG=1 CFLAGS="-g -O0"
```

Then use gdb:
```bash
gdb ./build/bin/neowall
(gdb) break compositor_backend_init
(gdb) run -fv
```

### Static Analysis

Check code quality:

```bash
# Check for issues
cppcheck src/compositor/

# Check for memory leaks
valgrind --leak-check=full ./build/bin/neowall
```

## 🚀 Next Steps

After successful build integration:

1. **Test** on multiple compositors
2. **Migrate** existing code to use abstraction (see INTEGRATION_EXAMPLE.md)
3. **Implement** KDE Plasma backend
4. **Implement** GNOME Shell backend
5. **Document** compositor compatibility
6. **Update** user-facing documentation

## 📞 Support

If you encounter build issues:

1. Check this document
2. Verify all files are present
3. Check compiler output carefully
4. Try clean build: `make clean && make`
5. Check Makefile syntax
6. Open GitHub issue with build log

---

**Build integration complete!** The compositor abstraction layer is now part of NeoWall's build system.