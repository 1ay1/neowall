# Staticwall - A reliable Wayland wallpaper daemon with Multi-Version EGL/OpenGL ES Support
# Copyright (C) 2024

PROJECT = staticwall
VERSION = 0.2.0

# Directories
SRC_DIR = src
EGL_DIR = $(SRC_DIR)/egl
INC_DIR = include
EGL_INC_DIR = $(INC_DIR)/egl
PROTO_DIR = protocols
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
EGL_OBJ_DIR = $(OBJ_DIR)/egl
BIN_DIR = $(BUILD_DIR)/bin
ASSETS_DIR = assets

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -I$(INC_DIR) -I$(PROTO_DIR)
LDFLAGS = -lwayland-client -lwayland-egl -lpthread -lm

# Color output for pretty printing
COLOR_BLUE = \033[1;34m
COLOR_GREEN = \033[1;32m
COLOR_YELLOW = \033[1;33m
COLOR_RED = \033[1;31m
COLOR_RESET = \033[0m

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
    $(info $(COLOR_GREEN)✓ EGL detected: $(EGL_VERSION)$(COLOR_RESET))
else
    $(error $(COLOR_RED)✗ EGL not found - required for Staticwall$(COLOR_RESET))
endif

# OpenGL ES 1.x support (optional, for legacy compatibility)
ifeq ($(HAS_GLES1_CM),yes)
    CFLAGS += -DHAVE_GLES1
    LDFLAGS += -lGLESv1_CM
    GLES1_SUPPORT := yes
    $(info $(COLOR_YELLOW)✓ OpenGL ES 1.x detected (legacy support)$(COLOR_RESET))
else
    GLES1_SUPPORT := no
    $(info $(COLOR_BLUE)○ OpenGL ES 1.x not found (optional)$(COLOR_RESET))
endif

# OpenGL ES 2.0 support (required minimum)
ifeq ($(HAS_GLES2),yes)
    CFLAGS += -DHAVE_GLES2
    LDFLAGS += -lGLESv2
    GLES2_SUPPORT := yes
    $(info $(COLOR_GREEN)✓ OpenGL ES 2.0 detected$(COLOR_RESET))
else
    $(error $(COLOR_RED)✗ OpenGL ES 2.0 not found - minimum requirement$(COLOR_RESET))
endif

# OpenGL ES 3.0 support
ifeq ($(HAS_GLES30),yes)
    CFLAGS += -DHAVE_GLES3 -DHAVE_GLES30
    GLES30_SUPPORT := yes
    $(info $(COLOR_GREEN)✓ OpenGL ES 3.0 detected (enhanced Shadertoy support)$(COLOR_RESET))
else
    GLES30_SUPPORT := no
    $(info $(COLOR_YELLOW)○ OpenGL ES 3.0 not found (Shadertoy compatibility limited)$(COLOR_RESET))
endif

# OpenGL ES 3.1 support
ifeq ($(HAS_GLES31),yes)
    CFLAGS += -DHAVE_GLES31
    GLES31_SUPPORT := yes
    $(info $(COLOR_GREEN)✓ OpenGL ES 3.1 detected (compute shader support)$(COLOR_RESET))
else
    GLES31_SUPPORT := no
    $(info $(COLOR_BLUE)○ OpenGL ES 3.1 not found (optional)$(COLOR_RESET))
endif

# OpenGL ES 3.2 support
ifeq ($(HAS_GLES32),yes)
    CFLAGS += -DHAVE_GLES32
    GLES32_SUPPORT := yes
    $(info $(COLOR_GREEN)✓ OpenGL ES 3.2 detected (geometry/tessellation shaders)$(COLOR_RESET))
else
    GLES32_SUPPORT := no
    $(info $(COLOR_BLUE)○ OpenGL ES 3.2 not found (optional)$(COLOR_RESET))
endif

# EGL extensions
ifeq ($(HAS_EGL_KHR_IMAGE),yes)
    CFLAGS += -DHAVE_EGL_KHR_IMAGE
    $(info $(COLOR_GREEN)✓ EGL_KHR_image extension available$(COLOR_RESET))
endif

ifeq ($(HAS_EGL_KHR_FENCE),yes)
    CFLAGS += -DHAVE_EGL_KHR_FENCE_SYNC
    $(info $(COLOR_GREEN)✓ EGL_KHR_fence_sync extension available$(COLOR_RESET))
endif

# ============================================================================
# Source Files - Conditional Based on Detected Versions
# ============================================================================

# Core source files (always compiled)
CORE_SOURCES = $(filter-out $(SRC_DIR)/egl.c, $(wildcard $(SRC_DIR)/*.c))
TRANSITION_SOURCES = $(wildcard $(SRC_DIR)/transitions/*.c)
TEXTURE_SOURCES = $(wildcard $(SRC_DIR)/textures/*.c)
PROTO_SOURCES = $(wildcard $(PROTO_DIR)/*.c)

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
ALL_SOURCES = $(CORE_SOURCES) $(EGL_SOURCES) $(TRANSITION_SOURCES) $(TEXTURE_SOURCES) $(PROTO_SOURCES)

# ============================================================================
# Object Files
# ============================================================================

CORE_OBJECTS = $(CORE_SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
EGL_OBJECTS = $(EGL_SOURCES:$(EGL_DIR)/%.c=$(EGL_OBJ_DIR)/%.o)
TRANSITION_OBJECTS = $(TRANSITION_SOURCES:$(SRC_DIR)/transitions/%.c=$(OBJ_DIR)/transitions_%.o)
TEXTURE_OBJECTS = $(TEXTURE_SOURCES:$(SRC_DIR)/textures/%.c=$(OBJ_DIR)/textures_%.o)
PROTO_OBJECTS = $(PROTO_SOURCES:$(PROTO_DIR)/%.c=$(OBJ_DIR)/proto_%.o)

ALL_OBJECTS = $(CORE_OBJECTS) $(EGL_OBJECTS) $(TRANSITION_OBJECTS) $(TEXTURE_OBJECTS) $(PROTO_OBJECTS)

# ============================================================================
# Wayland Protocol Files
# ============================================================================

WLR_LAYER_SHELL_XML = /usr/share/wayland-protocols/wlr-unstable/wlr-layer-shell-unstable-v1.xml
XDG_SHELL_XML = /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
VIEWPORTER_XML = /usr/share/wayland-protocols/stable/viewporter/viewporter.xml

PROTO_HEADERS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h \
                $(PROTO_DIR)/xdg-shell-client-protocol.h \
                $(PROTO_DIR)/viewporter-client-protocol.h

PROTO_SRCS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c \
             $(PROTO_DIR)/xdg-shell-client-protocol.c \
             $(PROTO_DIR)/viewporter-client-protocol.c

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
	@echo "$(COLOR_BLUE)"
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║           Staticwall - Multi-Version EGL/OpenGL ES             ║"
	@echo "║                    Version $(VERSION)                              ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"
	@echo "$(COLOR_RESET)"

# Success message
success:
	@echo ""
	@echo "$(COLOR_GREEN)╔════════════════════════════════════════════════════════════════╗$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)║                  BUILD SUCCESSFUL!                             ║$(COLOR_RESET)"
	@echo "$(COLOR_GREEN)╚════════════════════════════════════════════════════════════════╝$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_BLUE)Binary:$(COLOR_RESET) $(TARGET)"
	@echo "$(COLOR_BLUE)Supported versions:$(COLOR_RESET)"
ifeq ($(GLES32_SUPPORT),yes)
	@echo "  $(COLOR_GREEN)✓$(COLOR_RESET) OpenGL ES 3.2 (geometry/tessellation shaders)"
endif
ifeq ($(GLES31_SUPPORT),yes)
	@echo "  $(COLOR_GREEN)✓$(COLOR_RESET) OpenGL ES 3.1 (compute shaders)"
endif
ifeq ($(GLES30_SUPPORT),yes)
	@echo "  $(COLOR_GREEN)✓$(COLOR_RESET) OpenGL ES 3.0 (enhanced Shadertoy)"
endif
ifeq ($(GLES2_SUPPORT),yes)
	@echo "  $(COLOR_GREEN)✓$(COLOR_RESET) OpenGL ES 2.0 (baseline)"
endif
ifeq ($(GLES1_SUPPORT),yes)
	@echo "  $(COLOR_YELLOW)✓$(COLOR_RESET) OpenGL ES 1.x (legacy)"
endif
	@echo ""
	@echo "$(COLOR_BLUE)Run:$(COLOR_RESET) ./$(TARGET)"
	@echo "$(COLOR_BLUE)Install:$(COLOR_RESET) sudo make install"
	@echo ""

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR) $(EGL_OBJ_DIR) $(BIN_DIR) $(PROTO_DIR)

# Generate Wayland protocol files
protocols: $(PROTO_HEADERS) $(PROTO_SRCS)

$(PROTO_DIR)/%-client-protocol.h: | directories
	@if [ -f "$(WLR_LAYER_SHELL_XML)" ]; then \
		echo "$(COLOR_BLUE)Generating wlr-layer-shell header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(WLR_LAYER_SHELL_XML) $@ 2>/dev/null || echo "$(COLOR_YELLOW)Warning: Could not generate wlr-layer-shell header$(COLOR_RESET)"; \
	fi
	@if [ -f "$(XDG_SHELL_XML)" ]; then \
		echo "$(COLOR_BLUE)Generating xdg-shell header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.h 2>/dev/null || echo "$(COLOR_YELLOW)Warning: Could not generate xdg-shell header$(COLOR_RESET)"; \
	fi
	@if [ -f "$(VIEWPORTER_XML)" ]; then \
		echo "$(COLOR_BLUE)Generating viewporter header...$(COLOR_RESET)"; \
		wayland-scanner client-header $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.h 2>/dev/null || echo "$(COLOR_YELLOW)Warning: Could not generate viewporter header$(COLOR_RESET)"; \
	fi

$(PROTO_DIR)/%-client-protocol.c: | directories
	@if [ -f "$(WLR_LAYER_SHELL_XML)" ]; then \
		echo "$(COLOR_BLUE)Generating wlr-layer-shell code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(WLR_LAYER_SHELL_XML) $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c 2>/dev/null || echo "$(COLOR_YELLOW)Warning: Could not generate wlr-layer-shell code$(COLOR_RESET)"; \
	fi
	@if [ -f "$(XDG_SHELL_XML)" ]; then \
		echo "$(COLOR_BLUE)Generating xdg-shell code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.c 2>/dev/null || echo "$(COLOR_YELLOW)Warning: Could not generate xdg-shell code$(COLOR_RESET)"; \
	fi
	@if [ -f "$(VIEWPORTER_XML)" ]; then \
		echo "$(COLOR_BLUE)Generating viewporter code...$(COLOR_RESET)"; \
		wayland-scanner private-code $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.c 2>/dev/null || echo "$(COLOR_YELLOW)Warning: Could not generate viewporter code$(COLOR_RESET)"; \
	fi

# Compile protocol objects
$(OBJ_DIR)/proto_%.o: $(PROTO_DIR)/%.c $(PROTO_HEADERS) | directories
	@echo "$(COLOR_BLUE)Compiling protocol:$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -Wno-unused-parameter -c $< -o $@

# Compile core source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | directories protocols
	@echo "$(COLOR_BLUE)Compiling:$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile EGL source files
$(EGL_OBJ_DIR)/%.o: $(EGL_DIR)/%.c | directories protocols
	@echo "$(COLOR_BLUE)Compiling EGL:$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile transition files
$(OBJ_DIR)/transitions_%.o: $(SRC_DIR)/transitions/%.c | directories protocols
	@echo "$(COLOR_BLUE)Compiling transition:$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile texture files
$(OBJ_DIR)/textures_%.o: $(SRC_DIR)/textures/%.c | directories protocols
	@echo "$(COLOR_BLUE)Compiling texture:$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link the binary
$(TARGET): $(ALL_OBJECTS)
	@echo "$(COLOR_GREEN)Linking:$(COLOR_RESET) $(TARGET)"
	@$(CC) $(ALL_OBJECTS) -o $@ $(LDFLAGS)

# ============================================================================
# Installation
# ============================================================================

PREFIX ?= /usr/local
DESTDIR ?=

install: $(TARGET)
	@echo "$(COLOR_GREEN)Installing Staticwall...$(COLOR_RESET)"
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	install -Dm644 config/staticwall.vibe $(DESTDIR)$(PREFIX)/share/$(PROJECT)/config.vibe.example
	install -Dm644 $(ASSETS_DIR)/default.png $(DESTDIR)$(PREFIX)/share/$(PROJECT)/default.png
	@mkdir -p $(DESTDIR)$(PREFIX)/share/$(PROJECT)/shaders
	@for shader in examples/shaders/*.glsl; do \
		install -Dm644 $$shader $(DESTDIR)$(PREFIX)/share/$(PROJECT)/shaders/$$(basename $$shader); \
	done
	@if [ -f examples/shaders/README.md ]; then \
		install -Dm644 examples/shaders/README.md $(DESTDIR)$(PREFIX)/share/$(PROJECT)/shaders/README.md; \
	fi
	@echo ""
	@echo "$(COLOR_GREEN)Installation complete!$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)Installed to:$(COLOR_RESET) $(DESTDIR)$(PREFIX)/bin/$(PROJECT)"
	@echo "$(COLOR_BLUE)Example config:$(COLOR_RESET) $(DESTDIR)$(PREFIX)/share/$(PROJECT)/config.vibe.example"
	@echo "$(COLOR_BLUE)Example shaders:$(COLOR_RESET) $(DESTDIR)$(PREFIX)/share/$(PROJECT)/shaders/"
	@echo ""
	@echo "$(COLOR_YELLOW)Note:$(COLOR_RESET) Staticwall runs as normal user. No sudo needed to run!"
	@echo "$(COLOR_YELLOW)On first run, config will be copied to ~/.config/staticwall/$(COLOR_RESET)"
	@echo ""

# Uninstall
uninstall:
	@echo "$(COLOR_RED)Uninstalling Staticwall...$(COLOR_RESET)"
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROJECT)
	rm -rf $(DESTDIR)$(PREFIX)/share/$(PROJECT)
	@echo "$(COLOR_GREEN)Uninstalled$(COLOR_RESET)"

# ============================================================================
# Cleaning
# ============================================================================

clean:
	@echo "$(COLOR_YELLOW)Cleaning build directory...$(COLOR_RESET)"
	rm -rf $(BUILD_DIR)
	@echo "$(COLOR_GREEN)Cleaned$(COLOR_RESET)"

distclean: clean
	@echo "$(COLOR_YELLOW)Cleaning generated protocol files...$(COLOR_RESET)"
	rm -rf $(PROTO_DIR)/*.c $(PROTO_DIR)/*.h
	@echo "$(COLOR_GREEN)All generated files cleaned$(COLOR_RESET)"

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
	@echo "$(COLOR_GREEN)Debug build complete$(COLOR_RESET)"

# Run with capability detection debugging
run-capabilities: $(TARGET)
	./$(TARGET) -f -v --debug-capabilities

# Print detected capabilities
print-caps:
	@echo "$(COLOR_BLUE)Detected Capabilities:$(COLOR_RESET)"
	@echo "  EGL: $(if $(HAS_EGL),$(COLOR_GREEN)Yes$(COLOR_RESET) ($(EGL_VERSION)),$(COLOR_RED)No$(COLOR_RESET))"
	@echo "  OpenGL ES 1.x: $(if $(filter yes,$(GLES1_SUPPORT)),$(COLOR_GREEN)Yes$(COLOR_RESET),$(COLOR_YELLOW)No$(COLOR_RESET))"
	@echo "  OpenGL ES 2.0: $(if $(filter yes,$(GLES2_SUPPORT)),$(COLOR_GREEN)Yes$(COLOR_RESET),$(COLOR_RED)No$(COLOR_RESET))"
	@echo "  OpenGL ES 3.0: $(if $(filter yes,$(GLES30_SUPPORT)),$(COLOR_GREEN)Yes$(COLOR_RESET),$(COLOR_YELLOW)No$(COLOR_RESET))"
	@echo "  OpenGL ES 3.1: $(if $(filter yes,$(GLES31_SUPPORT)),$(COLOR_GREEN)Yes$(COLOR_RESET),$(COLOR_YELLOW)No$(COLOR_RESET))"
	@echo "  OpenGL ES 3.2: $(if $(filter yes,$(GLES32_SUPPORT)),$(COLOR_GREEN)Yes$(COLOR_RESET),$(COLOR_YELLOW)No$(COLOR_RESET))"

# Code formatting (if clang-format is available)
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "$(COLOR_BLUE)Formatting code...$(COLOR_RESET)"; \
		find $(SRC_DIR) $(INC_DIR) -name '*.c' -o -name '*.h' | xargs clang-format -i; \
		echo "$(COLOR_GREEN)Code formatted$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)clang-format not found, skipping$(COLOR_RESET)"; \
	fi

# Static analysis (if cppcheck is available)
analyze:
	@if command -v cppcheck >/dev/null 2>&1; then \
		echo "$(COLOR_BLUE)Running static analysis...$(COLOR_RESET)"; \
		cppcheck --enable=all --suppress=missingIncludeSystem $(SRC_DIR) 2>&1; \
		echo "$(COLOR_GREEN)Analysis complete$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_YELLOW)cppcheck not found, skipping$(COLOR_RESET)"; \
	fi

# ============================================================================
# Help
# ============================================================================

help:
	@echo "$(COLOR_BLUE)╔════════════════════════════════════════════════════════════════╗$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)║           Staticwall Multi-Version Build System                ║$(COLOR_RESET)"
	@echo "$(COLOR_BLUE)╚════════════════════════════════════════════════════════════════╝$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_GREEN)Build Targets:$(COLOR_RESET)"
	@echo "  $(COLOR_BLUE)all$(COLOR_RESET)              - Build the project (default)"
	@echo "  $(COLOR_BLUE)debug$(COLOR_RESET)            - Build with debug symbols and no optimization"
	@echo "  $(COLOR_BLUE)clean$(COLOR_RESET)            - Remove build artifacts"
	@echo "  $(COLOR_BLUE)distclean$(COLOR_RESET)        - Remove all generated files"
	@echo ""
	@echo "$(COLOR_GREEN)Installation:$(COLOR_RESET)"
	@echo "  $(COLOR_BLUE)install$(COLOR_RESET)          - Install to system (requires sudo)"
	@echo "  $(COLOR_BLUE)uninstall$(COLOR_RESET)        - Remove from system (requires sudo)"
	@echo ""
	@echo "$(COLOR_GREEN)Running:$(COLOR_RESET)"
	@echo "  $(COLOR_BLUE)run$(COLOR_RESET)              - Build and run"
	@echo "  $(COLOR_BLUE)run-verbose$(COLOR_RESET)      - Build and run with verbose logging"
	@echo "  $(COLOR_BLUE)run-capabilities$(COLOR_RESET) - Run with capability detection debugging"
	@echo ""
	@echo "$(COLOR_GREEN)Development:$(COLOR_RESET)"
	@echo "  $(COLOR_BLUE)print-caps$(COLOR_RESET)       - Show detected EGL/OpenGL ES capabilities"
	@echo "  $(COLOR_BLUE)format$(COLOR_RESET)           - Format code with clang-format"
	@echo "  $(COLOR_BLUE)analyze$(COLOR_RESET)          - Run static analysis with cppcheck"
	@echo "  $(COLOR_BLUE)help$(COLOR_RESET)             - Show this help"
	@echo ""
	@echo "$(COLOR_GREEN)Variables:$(COLOR_RESET)"
	@echo "  $(COLOR_BLUE)PREFIX$(COLOR_RESET)           - Installation prefix (default: /usr/local)"
	@echo "  $(COLOR_BLUE)DESTDIR$(COLOR_RESET)          - Staging directory for installation"
	@echo "  $(COLOR_BLUE)CC$(COLOR_RESET)               - C compiler (default: gcc)"
	@echo ""
	@echo "$(COLOR_YELLOW)Note:$(COLOR_RESET) The build system automatically detects available EGL/OpenGL ES"
	@echo "      versions and compiles only the supported features."
	@echo ""

# ============================================================================
# Phony Targets
# ============================================================================

.PHONY: all banner success directories protocols clean distclean install uninstall \
        run run-verbose run-capabilities debug print-caps format analyze help

# Prevent make from deleting intermediate files
.PRECIOUS: $(PROTO_HEADERS) $(PROTO_SRCS)
