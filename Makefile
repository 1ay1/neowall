# NeoWall - A reliable Wayland wallpaper daemon with Multi-Version EGL/OpenGL ES Support
# Copyright (C) 2024

PROJECT = neowall
VERSION = 0.3.0

# Directories
SRC_DIR = src
EGL_DIR = $(SRC_DIR)/egl
COMPOSITOR_DIR = $(SRC_DIR)/compositor
COMPOSITOR_BACKEND_DIR = $(COMPOSITOR_DIR)/backends
INC_DIR = include
EGL_INC_DIR = $(INC_DIR)/egl
PROTO_DIR = protocols
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
EGL_OBJ_DIR = $(OBJ_DIR)/egl
COMPOSITOR_OBJ_DIR = $(OBJ_DIR)/compositor
COMPOSITOR_BACKEND_OBJ_DIR = $(OBJ_DIR)/compositor/backends
BIN_DIR = $(BUILD_DIR)/bin
ASSETS_DIR = assets

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -I$(INC_DIR) -I$(PROTO_DIR)
LDFLAGS = -lwayland-client -lwayland-egl -lpthread -lm



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

# ============================================================================
# Source Files - Conditional Based on Detected Versions
# ============================================================================

# Core source files (always compiled)
CORE_SOURCES = $(filter-out $(SRC_DIR)/egl.c, $(wildcard $(SRC_DIR)/*.c))
TRANSITION_SOURCES = $(wildcard $(SRC_DIR)/transitions/*.c)
TEXTURE_SOURCES = $(wildcard $(SRC_DIR)/textures/*.c)
PROTO_SOURCES = $(wildcard $(PROTO_DIR)/*.c)

# Compositor abstraction layer sources
COMPOSITOR_SOURCES = $(COMPOSITOR_DIR)/compositor_registry.c \
                     $(COMPOSITOR_DIR)/compositor_surface.c

# Compositor backend sources
COMPOSITOR_BACKEND_SOURCES = $(COMPOSITOR_BACKEND_DIR)/wlr_layer_shell.c \
                             $(COMPOSITOR_BACKEND_DIR)/kde_plasma.c \
                             $(COMPOSITOR_BACKEND_DIR)/gnome_shell.c \
                             $(COMPOSITOR_BACKEND_DIR)/fallback.c

# EGL core (always compiled)
EGL_CORE_SOURCES = $(EGL_DIR)/capability.c $(EGL_DIR)/egl_core.c

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
ALL_SOURCES = $(CORE_SOURCES) $(EGL_SOURCES) $(TRANSITION_SOURCES) $(TEXTURE_SOURCES) $(PROTO_SOURCES) $(COMPOSITOR_SOURCES) $(COMPOSITOR_BACKEND_SOURCES)

# ============================================================================
# Object Files
# ============================================================================

CORE_OBJECTS = $(CORE_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
EGL_OBJECTS = $(EGL_SOURCES:$(EGL_DIR)/%.c=$(EGL_OBJ_DIR)/%.o)
TRANSITION_OBJECTS = $(TRANSITION_SOURCES:$(SRC_DIR)/transitions/%.c=$(OBJ_DIR)/transitions_%.o)
TEXTURE_OBJECTS = $(TEXTURE_SOURCES:$(SRC_DIR)/textures/%.c=$(OBJ_DIR)/textures_%.o)
PROTO_OBJECTS = $(PROTO_SOURCES:$(PROTO_DIR)/%.c=$(OBJ_DIR)/proto_%.o)
COMPOSITOR_OBJECTS = $(COMPOSITOR_SOURCES:$(COMPOSITOR_DIR)/%.c=$(COMPOSITOR_OBJ_DIR)/%.o)
COMPOSITOR_BACKEND_OBJECTS = $(COMPOSITOR_BACKEND_SOURCES:$(COMPOSITOR_BACKEND_DIR)/%.c=$(COMPOSITOR_BACKEND_OBJ_DIR)/%.o)

ALL_OBJECTS = $(CORE_OBJECTS) $(EGL_OBJECTS) $(TRANSITION_OBJECTS) $(TEXTURE_OBJECTS) $(PROTO_OBJECTS) $(COMPOSITOR_OBJECTS) $(COMPOSITOR_BACKEND_OBJECTS)

# ============================================================================
# Wayland Protocol Files
# ============================================================================

WLR_LAYER_SHELL_XML = /usr/share/wayland-protocols/wlr-unstable/wlr-layer-shell-unstable-v1.xml
XDG_SHELL_XML = /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
VIEWPORTER_XML = /usr/share/wayland-protocols/stable/viewporter/viewporter.xml
XDG_OUTPUT_XML = /usr/share/wayland-protocols/unstable/xdg-output/xdg-output-unstable-v1.xml
PLASMA_SHELL_XML = $(PROTO_DIR)/plasma-shell.xml

PROTO_HEADERS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h \
                $(PROTO_DIR)/xdg-shell-client-protocol.h \
                $(PROTO_DIR)/viewporter-client-protocol.h \
                $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.h \
                $(PROTO_DIR)/plasma-shell-client-protocol.h

PROTO_SRCS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c \
             $(PROTO_DIR)/xdg-shell-client-protocol.c \
             $(PROTO_DIR)/viewporter-client-protocol.c \
             $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.c \
             $(PROTO_DIR)/plasma-shell-client-protocol.c

# ============================================================================
# pkg-config Dependencies
# ============================================================================

DEPS = wayland-client wayland-egl
CFLAGS += $(shell pkg-config --cflags $(DEPS))
LDFLAGS += $(shell pkg-config --libs $(DEPS))

# Optional dependencies (image loading)
OPTIONAL_DEPS = libpng libjpeg
CFLAGS += $(shell pkg-config --cflags $(OPTIONAL_DEPS) 2>/dev/null || echo "")
LDFLAGS += $(shell pkg-config --libs $(OPTIONAL_DEPS) 2>/dev/null || echo "-lpng -ljpeg")

# ============================================================================
# Build Targets
# ============================================================================

# Target binary
TARGET = $(BIN_DIR)/$(PROJECT)

# Default target
all: banner directories protocols $(TARGET) success

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
	@mkdir -p $(OBJ_DIR) $(EGL_OBJ_DIR) $(COMPOSITOR_OBJ_DIR) $(COMPOSITOR_BACKEND_OBJ_DIR) $(BIN_DIR) $(PROTO_DIR)

# Generate Wayland protocol files
protocols: $(PROTO_HEADERS) $(PROTO_SRCS)

$(PROTO_DIR)/%-client-protocol.h: | directories
	@if [ -f "$(WLR_LAYER_SHELL_XML)" ]; then \
		echo "Generating wlr-layer-shell header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(WLR_LAYER_SHELL_XML) $@ 2>/dev/null || echo "Warning: Could not generate wlr-layer-shell header$(COLOR_RESET)"; \
	fi
	@if [ -f "$(XDG_SHELL_XML)" ]; then \
		echo "Generating xdg-shell header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.h 2>/dev/null || echo "Warning: Could not generate xdg-shell header$(COLOR_RESET)"; \
	fi
	@if [ -f "$(VIEWPORTER_XML)" ]; then \
		echo "Generating viewporter header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.h 2>/dev/null || echo "Warning: Could not generate viewporter header$(COLOR_RESET)"; \
	fi
	@if [ -f "$(XDG_OUTPUT_XML)" ]; then \
		echo "Generating xdg-output header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(XDG_OUTPUT_XML) $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.h 2>/dev/null || echo "Warning: Could not generate xdg-output header$(COLOR_RESET)"; \
	fi
	@if [ -f "$(PLASMA_SHELL_XML)" ]; then \
		echo "Generating plasma-shell header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(PLASMA_SHELL_XML) $(PROTO_DIR)/plasma-shell-client-protocol.h 2>/dev/null || echo "Warning: Could not generate plasma-shell header$(COLOR_RESET)"; \
	fi

$(PROTO_DIR)/%-client-protocol.c: | directories
	@if [ -f "$(WLR_LAYER_SHELL_XML)" ]; then \
		echo "Generating wlr-layer-shell code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(WLR_LAYER_SHELL_XML) $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c 2>/dev/null || echo "Warning: Could not generate wlr-layer-shell code$(COLOR_RESET)"; \
	fi
	@if [ -f "$(XDG_SHELL_XML)" ]; then \
		echo "Generating xdg-shell code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.c 2>/dev/null || echo "Warning: Could not generate xdg-shell code$(COLOR_RESET)"; \
	fi
	@if [ -f "$(VIEWPORTER_XML)" ]; then \
		echo "Generating viewporter code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.c 2>/dev/null || echo "Warning: Could not generate viewporter code$(COLOR_RESET)"; \
	fi
	@if [ -f "$(XDG_OUTPUT_XML)" ]; then \
		echo "Generating xdg-output code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(XDG_OUTPUT_XML) $(PROTO_DIR)/xdg-output-unstable-v1-client-protocol.c 2>/dev/null || echo "Warning: Could not generate xdg-output code$(COLOR_RESET)"; \
	fi
	@if [ -f "$(PLASMA_SHELL_XML)" ]; then \
		echo "Generating plasma-shell code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(PLASMA_SHELL_XML) $(PROTO_DIR)/plasma-shell-client-protocol.c 2>/dev/null || echo "Warning: Could not generate plasma-shell code$(COLOR_RESET)"; \
	fi

# Compile protocol objects
$(OBJ_DIR)/proto_%.o: $(PROTO_DIR)/%.c $(PROTO_HEADERS) | directories
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

# Compile transition files
$(OBJ_DIR)/transitions_%.o: $(SRC_DIR)/transitions/%.c | directories protocols
	@echo "Compiling transition: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile texture files
$(OBJ_DIR)/textures_%.o: $(SRC_DIR)/textures/%.c | directories protocols
	@echo "Compiling texture: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile compositor abstraction layer files
$(COMPOSITOR_OBJ_DIR)/%.o: $(COMPOSITOR_DIR)/%.c | directories protocols
	@echo "Compiling compositor: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile compositor backend files
$(COMPOSITOR_BACKEND_OBJ_DIR)/%.o: $(COMPOSITOR_BACKEND_DIR)/%.c | directories protocols
	@echo "Compiling compositor backend: $<"
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

.PHONY: all banner success directories protocols clean distclean install uninstall \
        run run-verbose run-capabilities debug print-caps format analyze help

# Prevent make from deleting intermediate files
.PRECIOUS: $(PROTO_HEADERS) $(PROTO_SRCS)
