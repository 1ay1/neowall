# NeoWall - A reliable Wayland wallpaper daemon with Multi-Version EGL/OpenGL ES Support
# Copyright (C) 2025

PROJECT = neowall
VERSION = 0.4.3

# Directories
SRC_DIR = src
EGL_DIR = $(SRC_DIR)/egl
SHADER_LIB_DIR = $(SRC_DIR)/shader_lib
COMPOSITOR_DIR = $(SRC_DIR)/compositor
COMPOSITOR_BACKEND_DIR = $(COMPOSITOR_DIR)/backends
WAYLAND_BACKEND_DIR = $(COMPOSITOR_BACKEND_DIR)/wayland
WAYLAND_COMPOSITOR_DIR = $(WAYLAND_BACKEND_DIR)/compositors
OUTPUT_DIR = $(SRC_DIR)/output
CONFIG_DIR = $(SRC_DIR)/config
RENDER_DIR = $(SRC_DIR)/render
IMAGE_DIR = $(SRC_DIR)/image
INC_DIR = include
EGL_INC_DIR = $(INC_DIR)/egl
PROTO_DIR = protocols
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
EGL_OBJ_DIR = $(OBJ_DIR)/egl
SHADER_LIB_OBJ_DIR = $(OBJ_DIR)/shader_lib
COMPOSITOR_OBJ_DIR = $(OBJ_DIR)/compositor
WAYLAND_BACKEND_OBJ_DIR = $(OBJ_DIR)/compositor/backends/wayland
WAYLAND_COMPOSITOR_OBJ_DIR = $(WAYLAND_BACKEND_OBJ_DIR)/compositors
OUTPUT_OBJ_DIR = $(OBJ_DIR)/output
CONFIG_OBJ_DIR = $(OBJ_DIR)/config
RENDER_OBJ_DIR = $(OBJ_DIR)/render
IMAGE_OBJ_DIR = $(OBJ_DIR)/image
BIN_DIR = $(BUILD_DIR)/bin
ASSETS_DIR = assets

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -I$(INC_DIR) -I$(PROTO_DIR) -I$(SHADER_LIB_DIR)
CFLAGS += -DNEOWALL_VERSION=\"$(VERSION)\"
LDFLAGS = -lpthread -lm

# Detect Wayland support
HAS_WAYLAND := $(shell pkg-config --exists wayland-client wayland-egl && echo yes)
HAS_WAYLAND_PROTOCOLS := $(shell pkg-config --exists wayland-protocols && echo yes)



# ============================================================================
# Automatic EGL and OpenGL ES Version Detection
# ============================================================================

# Detect EGL version
EGL_VERSION := $(shell pkg-config --modversion egl 2>/dev/null)
HAS_EGL := $(shell pkg-config --exists egl && echo yes)

# Detect OpenGL ES 1.x
HAS_GLES1_CM := $(shell pkg-config --exists glesv1_cm && echo yes)
HAS_GLES1 := $(shell test -f /usr/include/GLES/gl.h && echo yes)

# Detect OpenGL ES 2.0
HAS_GLES2 := $(shell pkg-config --exists glesv2 && echo yes)

# Detect OpenGL ES 3.0+
HAS_GLES3_HEADERS := $(shell test -f /usr/include/GLES3/gl3.h && echo yes)
HAS_GLES3_HEADERS_ALT := $(shell test -f /usr/include/GLES3/gl31.h && echo yes)
HAS_GLES3_HEADERS_ALT2 := $(shell test -f /usr/include/GLES3/gl32.h && echo yes)

# Check for specific GL ES 3.x versions
ifeq ($(HAS_GLES3_HEADERS),yes)
    HAS_GLES30 := yes
endif

ifeq ($(HAS_GLES3_HEADERS_ALT),yes)
    HAS_GLES31 := yes
    HAS_GLES30 := yes
endif

ifeq ($(HAS_GLES3_HEADERS_ALT2),yes)
    HAS_GLES32 := yes
    HAS_GLES31 := yes
    HAS_GLES30 := yes
endif

# Fallback: Try to detect via pkg-config
ifeq ($(HAS_GLES30),)
    HAS_GLES30 := $(shell pkg-config --exists glesv3 && echo yes)
endif

# Check EGL extensions support
HAS_EGL_KHR_IMAGE := $(shell grep -q "EGL_KHR_image" /usr/include/EGL/eglext.h 2>/dev/null && echo yes)
HAS_EGL_KHR_FENCE := $(shell grep -q "EGL_KHR_fence_sync" /usr/include/EGL/eglext.h 2>/dev/null && echo yes)

# Detect X11 support
HAS_X11 := $(shell pkg-config --exists x11 && echo yes)
HAS_XRANDR := $(shell pkg-config --exists xrandr && echo yes)

# ============================================================================
# Conditional Compilation Flags
# ============================================================================

# Base EGL support (required)
ifeq ($(HAS_EGL),yes)
    CFLAGS += -DHAVE_EGL
    LDFLAGS += -lEGL
    $(info EGL detected: $(EGL_VERSION))
else
    $(error EGL not found - required for NeoWall)
endif

# OpenGL ES 1.x support (optional, for legacy compatibility)
ifeq ($(HAS_GLES1_CM),yes)
    CFLAGS += -DHAVE_GLES1
    LDFLAGS += -lGLESv1_CM
    GLES1_SUPPORT := yes
    $(info OpenGL ES 1.x detected (legacy support))
else
    GLES1_SUPPORT := no
    $(info OpenGL ES 1.x not found (optional))
endif

# OpenGL ES 2.0 support (required minimum)
ifeq ($(HAS_GLES2),yes)
    CFLAGS += -DHAVE_GLES2
    LDFLAGS += -lGLESv2
    GLES2_SUPPORT := yes
    $(info OpenGL ES 2.0 detected)
else
    $(error x OpenGL ES 2.0 not found - minimum requirement$(COLOR_RESET))
endif

# OpenGL ES 3.0 support
ifeq ($(HAS_GLES30),yes)
    CFLAGS += -DHAVE_GLES3 -DHAVE_GLES30
    GLES30_SUPPORT := yes
    $(info OpenGL ES 3.0 detected (enhanced Shadertoy support))
else
    GLES30_SUPPORT := no
    $(info o OpenGL ES 3.0 not found (Shadertoy compatibility limited)$(COLOR_RESET))
endif

# OpenGL ES 3.1 support
ifeq ($(HAS_GLES31),yes)
    CFLAGS += -DHAVE_GLES31
    GLES31_SUPPORT := yes
    $(info OpenGL ES 3.1 detected (compute shader support))
else
    GLES31_SUPPORT := no
    $(info o OpenGL ES 3.1 not found (optional)$(COLOR_RESET))
endif

# OpenGL ES 3.2 support
ifeq ($(HAS_GLES32),yes)
    CFLAGS += -DHAVE_GLES32
    GLES32_SUPPORT := yes
    $(info OpenGL ES 3.2 detected (geometry/tessellation shaders))
else
    GLES32_SUPPORT := no
    $(info o OpenGL ES 3.2 not found (optional)$(COLOR_RESET))
endif

# EGL extensions
ifeq ($(HAS_EGL_KHR_IMAGE),yes)
    CFLAGS += -DHAVE_EGL_KHR_IMAGE
    $(info EGL_KHR_image extension available)
endif

ifeq ($(HAS_EGL_KHR_FENCE),yes)
    CFLAGS += -DHAVE_EGL_KHR_FENCE_SYNC
    $(info EGL_KHR_fence_sync extension available)
endif

# Wayland backend support (requires wayland-client, wayland-egl, AND wayland-protocols)
# Can be explicitly disabled with: make WAYLAND_BACKEND=no
ifneq ($(WAYLAND_BACKEND),no)
ifeq ($(HAS_WAYLAND)$(HAS_WAYLAND_PROTOCOLS),yesyes)
    CFLAGS += -DHAVE_WAYLAND_BACKEND
    LDFLAGS += -lwayland-client -lwayland-egl
    WAYLAND_BACKEND := yes
    $(info Wayland backend available)
else
    WAYLAND_BACKEND := no
    $(info Wayland backend not available (requires wayland-client, wayland-egl, and wayland-protocols))
endif
else
    WAYLAND_BACKEND := no
    $(info Wayland backend disabled by user)
endif

# X11 backend support (requires both libX11 and libXrandr)
# Can be explicitly disabled with: make X11_BACKEND=no
ifneq ($(X11_BACKEND),no)
ifeq ($(HAS_X11)$(HAS_XRANDR),yesyes)
    CFLAGS += -DHAVE_X11_BACKEND -DHAVE_XRANDR
    LDFLAGS += -lX11 -lXrandr
    X11_BACKEND := yes
    $(info X11 backend available (with XRandR multi-monitor support))
else
    X11_BACKEND := no
    $(info X11 backend not available (requires libx11 and libxrandr))
endif
else
    X11_BACKEND := no
    $(info X11 backend disabled by user)
endif

# Ensure at least one backend is available
ifeq ($(WAYLAND_BACKEND)$(X11_BACKEND),nono)
    $(error No display server backend available. Install wayland-client/wayland-egl or libx11-dev)
endif

# ============================================================================
# Source Files - Conditional Based on Detected Versions
# ============================================================================

# Core source files (always compiled)
CORE_SOURCES = $(filter-out $(SRC_DIR)/egl.c $(SRC_DIR)/shader.c $(SRC_DIR)/shader_adaptation.c $(SRC_DIR)/shadertoy_compat.c, $(wildcard $(SRC_DIR)/*.c))
TRANSITION_SOURCES = $(wildcard $(SRC_DIR)/transitions/*.c)
TEXTURE_SOURCES = $(wildcard $(SRC_DIR)/textures/*.c)
OUTPUT_SOURCES = $(wildcard $(OUTPUT_DIR)/*.c)
CONFIG_SOURCES = $(wildcard $(CONFIG_DIR)/*.c)
RENDER_SOURCES = $(wildcard $(RENDER_DIR)/*.c)
IMAGE_SOURCES = $(wildcard $(IMAGE_DIR)/*.c)

# Shader library sources (from gleditor)
SHADER_LIB_SOURCES = $(SHADER_LIB_DIR)/shader_core.c \
                     $(SHADER_LIB_DIR)/shader_adaptation.c \
                     $(SHADER_LIB_DIR)/shadertoy_compat.c \
                     $(SHADER_LIB_DIR)/shader_utils.c \
                     $(SHADER_LIB_DIR)/neowall_shader_api.c

# Compositor abstraction layer sources
COMPOSITOR_SOURCES = $(COMPOSITOR_DIR)/compositor_registry.c \
                     $(COMPOSITOR_DIR)/compositor_surface.c

# Wayland backend sources (conditional)
ifeq ($(WAYLAND_BACKEND),yes)
    WAYLAND_BACKEND_SOURCES = $(WAYLAND_BACKEND_DIR)/wayland_core.c
    WAYLAND_COMPOSITOR_SOURCES = $(WAYLAND_COMPOSITOR_DIR)/wlr_layer_shell.c \
                                 $(WAYLAND_COMPOSITOR_DIR)/kde_plasma.c \
                                 $(WAYLAND_COMPOSITOR_DIR)/gnome_shell.c \
                                 $(WAYLAND_COMPOSITOR_DIR)/fallback.c
    PROTO_SOURCES = $(wildcard $(PROTO_DIR)/*.c)
else
    WAYLAND_BACKEND_SOURCES =
    WAYLAND_COMPOSITOR_SOURCES =
    PROTO_SOURCES =
endif

# EGL core (always compiled)
EGL_CORE_SOURCES = $(EGL_DIR)/capability.c $(EGL_DIR)/egl_core.c

# X11 backend sources (conditional - requires X11_BACKEND=yes)
ifeq ($(X11_BACKEND),yes)
    X11_BACKEND_DIR = $(SRC_DIR)/compositor/backends/x11
    X11_BACKEND_SOURCES = $(X11_BACKEND_DIR)/x11_core.c
else
    X11_BACKEND_SOURCES =
endif

# Version-specific EGL sources (always include for runtime detection)
EGL_VERSION_SOURCES = $(EGL_DIR)/egl_v10.c \
                      $(EGL_DIR)/egl_v11.c \
                      $(EGL_DIR)/egl_v12.c \
                      $(EGL_DIR)/egl_v13.c \
                      $(EGL_DIR)/egl_v14.c \
                      $(EGL_DIR)/egl_v15.c

# OpenGL ES version-specific sources (conditional)
GLES_SOURCES :=

ifeq ($(GLES1_SUPPORT),yes)
    GLES_SOURCES += $(EGL_DIR)/gles_v10.c $(EGL_DIR)/gles_v11.c
endif

# ES 2.0 always included (minimum requirement)
GLES_SOURCES += $(EGL_DIR)/gles_v20.c

ifeq ($(GLES30_SUPPORT),yes)
    GLES_SOURCES += $(EGL_DIR)/gles_v30.c
endif

ifeq ($(GLES31_SUPPORT),yes)
    GLES_SOURCES += $(EGL_DIR)/gles_v31.c
endif

ifeq ($(GLES32_SUPPORT),yes)
    GLES_SOURCES += $(EGL_DIR)/gles_v32.c
endif

# All EGL sources
EGL_SOURCES = $(EGL_CORE_SOURCES) $(EGL_VERSION_SOURCES) $(GLES_SOURCES)

# Combine all sources
ALL_SOURCES = $(CORE_SOURCES) $(EGL_SOURCES) $(SHADER_LIB_SOURCES) $(TRANSITION_SOURCES) $(TEXTURE_SOURCES) $(OUTPUT_SOURCES) $(CONFIG_SOURCES) $(RENDER_SOURCES) $(IMAGE_SOURCES) $(PROTO_SOURCES) $(COMPOSITOR_SOURCES) $(WAYLAND_BACKEND_SOURCES) $(WAYLAND_COMPOSITOR_SOURCES) $(X11_BACKEND_SOURCES)

# ============================================================================
# Object Files
# ============================================================================

CORE_OBJECTS = $(CORE_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
EGL_OBJECTS = $(EGL_SOURCES:$(EGL_DIR)/%.c=$(EGL_OBJ_DIR)/%.o)
SHADER_LIB_OBJECTS = $(SHADER_LIB_SOURCES:$(SHADER_LIB_DIR)/%.c=$(SHADER_LIB_OBJ_DIR)/%.o)
TRANSITION_OBJECTS = $(TRANSITION_SOURCES:$(SRC_DIR)/transitions/%.c=$(OBJ_DIR)/transitions_%.o)
TEXTURE_OBJECTS = $(TEXTURE_SOURCES:$(SRC_DIR)/textures/%.c=$(OBJ_DIR)/textures_%.o)
OUTPUT_OBJECTS = $(OUTPUT_SOURCES:$(OUTPUT_DIR)/%.c=$(OUTPUT_OBJ_DIR)/%.o)
CONFIG_OBJECTS = $(CONFIG_SOURCES:$(CONFIG_DIR)/%.c=$(CONFIG_OBJ_DIR)/%.o)
RENDER_OBJECTS = $(RENDER_SOURCES:$(RENDER_DIR)/%.c=$(RENDER_OBJ_DIR)/%.o)
IMAGE_OBJECTS = $(IMAGE_SOURCES:$(IMAGE_DIR)/%.c=$(IMAGE_OBJ_DIR)/%.o)
PROTO_OBJECTS = $(PROTO_SOURCES:$(PROTO_DIR)/%.c=$(OBJ_DIR)/proto_%.o)
COMPOSITOR_OBJECTS = $(COMPOSITOR_SOURCES:$(COMPOSITOR_DIR)/%.c=$(COMPOSITOR_OBJ_DIR)/%.o)
WAYLAND_BACKEND_OBJECTS = $(WAYLAND_BACKEND_SOURCES:$(WAYLAND_BACKEND_DIR)/%.c=$(WAYLAND_BACKEND_OBJ_DIR)/%.o)
WAYLAND_COMPOSITOR_OBJECTS = $(WAYLAND_COMPOSITOR_SOURCES:$(WAYLAND_COMPOSITOR_DIR)/%.c=$(WAYLAND_COMPOSITOR_OBJ_DIR)/%.o)

# X11 backend objects
X11_BACKEND_OBJ_DIR = $(OBJ_DIR)/compositor/backends/x11
X11_BACKEND_OBJECTS = $(X11_BACKEND_SOURCES:$(X11_BACKEND_DIR)/%.c=$(X11_BACKEND_OBJ_DIR)/%.o)

ALL_OBJECTS = $(CORE_OBJECTS) $(EGL_OBJECTS) $(SHADER_LIB_OBJECTS) $(TRANSITION_OBJECTS) $(TEXTURE_OBJECTS) $(OUTPUT_OBJECTS) $(CONFIG_OBJECTS) $(RENDER_OBJECTS) $(IMAGE_OBJECTS) $(PROTO_OBJECTS) $(COMPOSITOR_OBJECTS) $(WAYLAND_BACKEND_OBJECTS) $(WAYLAND_COMPOSITOR_OBJECTS) $(X11_BACKEND_OBJECTS)

# ============================================================================
# Wayland Protocol Files
# ============================================================================

WLR_LAYER_SHELL_XML = /usr/share/wayland-protocols/wlr-unstable/wlr-layer-shell-unstable-v1.xml
XDG_SHELL_XML = /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
VIEWPORTER_XML = /usr/share/wayland-protocols/stable/viewporter/viewporter.xml
XDG_OUTPUT_XML = /usr/share/wayland-protocols/unstable/xdg-output/xdg-output-unstable-v1.xml
PLASMA_SHELL_XML = $(PROTO_DIR)/plasma-shell.xml
TEARING_CONTROL_XML = $(PROTO_DIR)/tearing-control-v1.xml

# Protocol headers/sources only needed for Wayland backend
ifeq ($(WAYLAND_BACKEND),yes)
PROTO_HEADERS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h \
                $(PROTO_DIR)/xdg-shell-client-protocol.h \
                $(PROTO_DIR)/viewporter-client-protocol.h \
                $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.h \
                $(PROTO_DIR)/plasma-shell-client-protocol.h \
                $(PROTO_DIR)/tearing-control-v1-client-protocol.h

PROTO_SRCS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c \
             $(PROTO_DIR)/xdg-shell-client-protocol.c \
             $(PROTO_DIR)/viewporter-client-protocol.c \
             $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.c \
             $(PROTO_DIR)/plasma-shell-client-protocol.c \
             $(PROTO_DIR)/tearing-control-v1-client-protocol.c
else
PROTO_HEADERS =
PROTO_SRCS =
endif

# ============================================================================
# pkg-config Dependencies
# ============================================================================

# Wayland deps only when Wayland backend is enabled
ifeq ($(WAYLAND_BACKEND),yes)
DEPS = wayland-client wayland-egl
CFLAGS += $(shell pkg-config --cflags $(DEPS))
LDFLAGS += $(shell pkg-config --libs $(DEPS))
endif

# Optional dependencies (image loading)
OPTIONAL_DEPS = libpng libjpeg
CFLAGS += $(shell pkg-config --cflags $(OPTIONAL_DEPS) 2>/dev/null || echo "")
LDFLAGS += $(shell pkg-config --libs $(OPTIONAL_DEPS) 2>/dev/null || echo "-lpng -ljpeg")

# ============================================================================
# Build Targets
# ============================================================================

# Target binary
TARGET = $(BIN_DIR)/$(PROJECT)

# Default target - protocols only when Wayland backend is enabled
ifeq ($(WAYLAND_BACKEND),yes)
all: banner directories version_header protocols $(TARGET) success
else
all: banner directories version_header $(TARGET) success
endif

# Banner
banner:
	@echo ""
	@echo ""
	@echo ""
	@echo ""
	@echo ""
	@echo ""

# Success message
success:
	@echo ""
	@echo "$(COLOR_RESET)"
	@echo "$(COLOR_RESET)"
	@echo "$(COLOR_RESET)"
	@echo ""
	@echo "Binary:$(COLOR_RESET) $(TARGET)"
	@echo "Supported versions:$(COLOR_RESET)"
ifeq ($(GLES32_SUPPORT),yes)
	@echo "  +$(COLOR_RESET) OpenGL ES 3.2 (geometry/tessellation shaders)"
endif
ifeq ($(GLES31_SUPPORT),yes)
	@echo "  +$(COLOR_RESET) OpenGL ES 3.1 (compute shaders)"
endif
ifeq ($(GLES30_SUPPORT),yes)
	@echo "  +$(COLOR_RESET) OpenGL ES 3.0 (enhanced Shadertoy)"
endif
ifeq ($(GLES2_SUPPORT),yes)
	@echo "  +$(COLOR_RESET) OpenGL ES 2.0 (baseline)"
endif
ifeq ($(GLES1_SUPPORT),yes)
	@echo "  +$(COLOR_RESET) OpenGL ES 1.x (legacy)"
endif
	@echo ""
	@echo "Run:$(COLOR_RESET) ./$(TARGET)"
	@echo "Install:$(COLOR_RESET) sudo make install"
	@echo ""

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR) $(EGL_OBJ_DIR) $(SHADER_LIB_OBJ_DIR) $(COMPOSITOR_OBJ_DIR) $(WAYLAND_BACKEND_OBJ_DIR) $(WAYLAND_COMPOSITOR_OBJ_DIR) $(X11_BACKEND_OBJ_DIR) $(OUTPUT_OBJ_DIR) $(CONFIG_OBJ_DIR) $(RENDER_OBJ_DIR) $(IMAGE_OBJ_DIR) $(BIN_DIR) $(PROTO_DIR)

# Generate version.h from version.h.in
version_header: $(INC_DIR)/version.h

$(INC_DIR)/version.h: $(INC_DIR)/version.h.in
	@sed 's/@VERSION@/$(VERSION)/g' $< > $@
	@echo "Generated: $(INC_DIR)/version.h (v$(VERSION))"

# Wayland protocol files - use pre-generated files from protocols/ directory
# These are checked into git and should not need regeneration
# To regenerate, run: make regenerate-protocols (requires wayland-scanner and XML files)
protocols:
	@if [ "$(WAYLAND_BACKEND)" = "yes" ]; then \
		for f in $(PROTO_HEADERS); do \
			if [ ! -f "$$f" ]; then \
				echo "Error: Missing protocol header: $$f"; \
				echo "Protocol files should be pre-generated in the repository."; \
				exit 1; \
			fi; \
		done; \
		echo "Protocol headers verified"; \
	fi

# Optional target to regenerate protocol files (for development only)
regenerate-protocols: | directories
	@echo "Regenerating Wayland protocol files..."
	@if [ -f "$(WLR_LAYER_SHELL_XML)" ]; then \
		echo "Generating wlr-layer-shell..."; \
		wayland-scanner client-header $(WLR_LAYER_SHELL_XML) $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h; \
		wayland-scanner private-code $(WLR_LAYER_SHELL_XML) $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c; \
	fi
	@if [ -f "$(XDG_SHELL_XML)" ]; then \
		echo "Generating xdg-shell..."; \
		wayland-scanner client-header $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.h; \
		wayland-scanner private-code $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.c; \
	fi
	@if [ -f "$(VIEWPORTER_XML)" ]; then \
		echo "Generating viewporter..."; \
		wayland-scanner client-header $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.h; \
		wayland-scanner private-code $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.c; \
	fi
	@if [ -f "$(XDG_OUTPUT_XML)" ]; then \
		echo "Generating xdg-output..."; \
		wayland-scanner client-header $(XDG_OUTPUT_XML) $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.h; \
		wayland-scanner private-code $(XDG_OUTPUT_XML) $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.c; \
	fi
	@if [ -f "$(PLASMA_SHELL_XML)" ]; then \
		echo "Generating plasma-shell..."; \
		wayland-scanner client-header $(PLASMA_SHELL_XML) $(PROTO_DIR)/plasma-shell-client-protocol.h; \
		wayland-scanner private-code $(PLASMA_SHELL_XML) $(PROTO_DIR)/plasma-shell-client-protocol.c; \
	fi
	@if [ -f "$(TEARING_CONTROL_XML)" ]; then \
		echo "Generating tearing-control..."; \
		wayland-scanner client-header $(TEARING_CONTROL_XML) $(PROTO_DIR)/tearing-control-v1-client-protocol.h; \
		wayland-scanner private-code $(TEARING_CONTROL_XML) $(PROTO_DIR)/tearing-control-v1-client-protocol.c; \
	fi
	@echo "Protocol regeneration complete"

# Compile protocol objects
$(OBJ_DIR)/proto_%.o: $(PROTO_DIR)/%.c | directories
	@echo "Compiling protocol: $<"
	@$(CC) $(CFLAGS) -Wno-unused-parameter -c $< -o $@

# Compile core source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | directories protocols
	@echo "Compiling: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile EGL source files
$(EGL_OBJ_DIR)/%.o: $(EGL_DIR)/%.c | directories protocols
	@echo "Compiling EGL: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile shader library files
$(SHADER_LIB_OBJ_DIR)/%.o: $(SHADER_LIB_DIR)/%.c | directories protocols
	@echo "Compiling shader library: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile transition files
$(OBJ_DIR)/transitions_%.o: $(SRC_DIR)/transitions/%.c | directories protocols
	@echo "Compiling transition: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile texture files
$(OBJ_DIR)/textures_%.o: $(SRC_DIR)/textures/%.c | directories protocols
	@echo "Compiling texture: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile output files
$(OUTPUT_OBJ_DIR)/%.o: $(OUTPUT_DIR)/%.c | directories protocols
	@echo "Compiling output: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile config files
$(CONFIG_OBJ_DIR)/%.o: $(CONFIG_DIR)/%.c | directories protocols
	@echo "Compiling config: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile render files
$(RENDER_OBJ_DIR)/%.o: $(RENDER_DIR)/%.c | directories protocols
	@echo "Compiling render: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile image files
$(IMAGE_OBJ_DIR)/%.o: $(IMAGE_DIR)/%.c | directories protocols
	@echo "Compiling image: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile compositor abstraction layer files
$(COMPOSITOR_OBJ_DIR)/%.o: $(COMPOSITOR_DIR)/%.c | directories protocols
	@echo "Compiling compositor: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Wayland backend core files
$(WAYLAND_BACKEND_OBJ_DIR)/%.o: $(WAYLAND_BACKEND_DIR)/%.c | directories protocols
	@echo "Compiling Wayland backend: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile Wayland compositor-specific backend files
$(WAYLAND_COMPOSITOR_OBJ_DIR)/%.o: $(WAYLAND_COMPOSITOR_DIR)/%.c | directories protocols
	@echo "Compiling Wayland compositor: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile X11 backend files
$(X11_BACKEND_OBJ_DIR)/%.o: $(X11_BACKEND_DIR)/%.c | directories
	@echo "Compiling X11 backend: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link the binary
$(TARGET): $(ALL_OBJECTS)
	@echo "Linking: $(TARGET)"
	@$(CC) $(ALL_OBJECTS) -o $@ $(LDFLAGS)

# ============================================================================
# Installation
# ============================================================================

PREFIX ?= /usr/local
DESTDIR ?=

install: $(TARGET)
	@echo "Installing NeoWall..."
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	install -Dm644 config/config.vibe $(DESTDIR)$(PREFIX)/share/$(PROJECT)/config.vibe
	install -Dm644 config/neowall.vibe $(DESTDIR)$(PREFIX)/share/$(PROJECT)/neowall.vibe
	@mkdir -p $(DESTDIR)$(PREFIX)/share/$(PROJECT)/shaders
	@for shader in examples/shaders/*.glsl; do \
		install -Dm644 $$shader $(DESTDIR)$(PREFIX)/share/$(PROJECT)/shaders/$$(basename $$shader); \
	done
	@echo "Installation complete"
	@echo "Installed to: $(DESTDIR)$(PREFIX)/bin/$(PROJECT)"
	@echo "Config files: $(PREFIX)/share/$(PROJECT)/"
	@echo "Shaders: $(PREFIX)/share/$(PROJECT)/shaders/"
	@echo "On first run, files will be copied to ~/.config/neowall/"

# Uninstall
uninstall:
	@echo "Uninstalling NeoWall..."
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	rm -rf $(DESTDIR)$(PREFIX)/share/$(PROJECT)
	@echo "Uninstalled"

# ============================================================================
# Cleaning
# ============================================================================

clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILD_DIR)
	rm -f $(INC_DIR)/version.h
	@echo "Cleaned"

distclean: clean
	@echo "Cleaning generated protocol files..."
	rm -rf $(PROTO_DIR)/*.c $(PROTO_DIR)/*.h
	@echo "All generated files cleaned"

# ============================================================================
# Development Targets
# ============================================================================

# Run the daemon
run: $(TARGET)
	./$(TARGET)

# Run with verbose logging
run-verbose: $(TARGET)
	./$(TARGET) -f -v

# Debug build
debug: CFLAGS += -g -DDEBUG -O0
debug: clean $(TARGET)
	@echo "Debug build complete"

# Run with capability detection debugging
run-capabilities: $(TARGET)
	./$(TARGET) -f -v --debug-capabilities

# Print detected capabilities
print-caps:
	@echo "Detected Capabilities:"
	@echo "  EGL: $(if $(HAS_EGL),Yes ($(EGL_VERSION)),No)"
	@echo "  OpenGL ES 1.x: $(if $(filter yes,$(GLES1_SUPPORT)),Yes,No)"
	@echo "  OpenGL ES 2.0: $(if $(filter yes,$(GLES2_SUPPORT)),Yes,No)"
	@echo "  OpenGL ES 3.0: $(if $(filter yes,$(GLES30_SUPPORT)),Yes,No)"
	@echo "  OpenGL ES 3.1: $(if $(filter yes,$(GLES31_SUPPORT)),Yes,No)"
	@echo "  OpenGL ES 3.2: $(if $(filter yes,$(GLES32_SUPPORT)),Yes,No)"

# Code formatting (if clang-format is available)
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting code..."; \
		find $(SRC_DIR) $(INC_DIR) -name '*.c' -o -name '*.h' | xargs clang-format -i; \
		echo "Code formatted"; \
	else \
		echo "clang-format not found, skipping"; \
	fi

# Static analysis (if cppcheck is available)
analyze:
	@if command -v cppcheck >/dev/null 2>&1; then \
		echo "Running static analysis..."; \
		cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR) 2>&1; \
		echo "Analysis complete"; \
	else \
		echo "cppcheck not found, skipping"; \
	fi

# ============================================================================
# Help
# ============================================================================

help:
	@echo "NeoWall Build System"
	@echo ""
	@echo "Build Targets:"
	@echo "  all              - Build the project (default)"
	@echo "  debug            - Build with debug symbols and no optimization"
	@echo "  clean            - Remove build artifacts"
	@echo "  distclean        - Remove all generated files"
	@echo ""
	@echo "Installation:"
	@echo "  install          - Install to system (requires sudo)"
	@echo "  uninstall        - Remove from system (requires sudo)"
	@echo ""
	@echo "Running:"
	@echo "  run              - Build and run"
	@echo "  run-verbose      - Build and run with verbose logging"
	@echo "  run-capabilities - Run with capability detection debugging"
	@echo ""
	@echo "Development:"
	@echo "  print-caps       - Show detected EGL/OpenGL ES capabilities"
	@echo "  format           - Format code with clang-format"
	@echo "  analyze          - Run static analysis with cppcheck"
	@echo "  help             - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  PREFIX           - Installation prefix (default: /usr/local)"
	@echo "  DESTDIR          - Staging directory for installation"
	@echo "  CC               - C compiler (default: gcc)"
	@echo ""

# ============================================================================
# Phony Targets
# ============================================================================

.PHONY: all banner success directories version_header protocols clean distclean install uninstall \
        run run-verbose run-capabilities debug print-caps format analyze help

# Prevent make from deleting intermediate files
.PRECIOUS: $(PROTO_HEADERS) $(PROTO_SRCS)
