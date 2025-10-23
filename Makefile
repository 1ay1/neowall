# Staticwall - A reliable Wayland wallpaper daemon
# Copyright (C) 2024

PROJECT = staticwall
VERSION = 0.1.0

# Directories
SRC_DIR = src
INC_DIR = include
PROTO_DIR = protocols
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
ASSETS_DIR = assets

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -I$(INC_DIR) -I$(PROTO_DIR)
LDFLAGS = -lwayland-client -lwayland-egl -lEGL -lGLESv2 -lpthread -lm

# pkg-config dependencies
DEPS = wayland-client wayland-egl egl glesv2
CFLAGS += $(shell pkg-config --cflags $(DEPS))
LDFLAGS += $(shell pkg-config --libs $(DEPS))

# Optional dependencies (images)
OPTIONAL_DEPS = libpng libjpeg
CFLAGS += $(shell pkg-config --cflags $(OPTIONAL_DEPS) 2>/dev/null || echo "")
LDFLAGS += $(shell pkg-config --libs $(OPTIONAL_DEPS) 2>/dev/null || echo "-lpng -ljpeg")

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
TRANSITION_SOURCES = $(wildcard $(SRC_DIR)/transitions/*.c)
PROTO_SOURCES = $(wildcard $(PROTO_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
TRANSITION_OBJECTS = $(TRANSITION_SOURCES:$(SRC_DIR)/transitions/%.c=$(OBJ_DIR)/transitions_%.o)
PROTO_OBJECTS = $(PROTO_SOURCES:$(PROTO_DIR)/%.c=$(OBJ_DIR)/proto_%.o)

# Protocol files
WLR_LAYER_SHELL_XML = /usr/share/wayland-protocols/wlr-unstable/wlr-layer-shell-unstable-v1.xml
XDG_SHELL_XML = /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
VIEWPORTER_XML = /usr/share/wayland-protocols/stable/viewporter/viewporter.xml

PROTO_HEADERS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.h \
                $(PROTO_DIR)/xdg-shell-client-protocol.h \
                $(PROTO_DIR)/viewporter-client-protocol.h

PROTO_SRCS = $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c \
             $(PROTO_DIR)/xdg-shell-client-protocol.c \
             $(PROTO_DIR)/viewporter-client-protocol.c

# Target binary
TARGET = $(BIN_DIR)/$(PROJECT)

# Default target
all: directories protocols $(TARGET)

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(PROTO_DIR)

# Generate Wayland protocol files
protocols: $(PROTO_HEADERS) $(PROTO_SRCS)

$(PROTO_DIR)/%-client-protocol.h: | directories
	@if [ -f "/usr/share/wayland-protocols/wlr-unstable/wlr-layer-shell-unstable-v1.xml" ]; then \
		wayland-scanner client-header $(WLR_LAYER_SHELL_XML) $@ 2>/dev/null || echo "Warning: Could not generate wlr-layer-shell header"; \
	fi
	@if [ -f "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" ]; then \
		wayland-scanner client-header $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.h 2>/dev/null || echo "Warning: Could not generate xdg-shell header"; \
	fi
	@if [ -f "/usr/share/wayland-protocols/stable/viewporter/viewporter.xml" ]; then \
		wayland-scanner client-header $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.h 2>/dev/null || echo "Warning: Could not generate viewporter header"; \
	fi

$(PROTO_DIR)/%-client-protocol.c: | directories
	@if [ -f "/usr/share/wayland-protocols/wlr-unstable/wlr-layer-shell-unstable-v1.xml" ]; then \
		wayland-scanner private-code $(WLR_LAYER_SHELL_XML) $(PROTO_DIR)/wlr-layer-shell-unstable-v1-client-protocol.c 2>/dev/null || echo "Warning: Could not generate wlr-layer-shell code"; \
	fi
	@if [ -f "/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml" ]; then \
		wayland-scanner private-code $(XDG_SHELL_XML) $(PROTO_DIR)/xdg-shell-client-protocol.c 2>/dev/null || echo "Warning: Could not generate xdg-shell code"; \
	fi
	@if [ -f "/usr/share/wayland-protocols/stable/viewporter/viewporter.xml" ]; then \
		wayland-scanner private-code $(VIEWPORTER_XML) $(PROTO_DIR)/viewporter-client-protocol.c 2>/dev/null || echo "Warning: Could not generate viewporter code"; \
	fi

# Compile protocol objects
$(OBJ_DIR)/proto_%.o: $(PROTO_DIR)/%.c $(PROTO_HEADERS) | directories
	$(CC) $(CFLAGS) -Wno-unused-parameter -c $< -o $@

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | directories protocols
	$(CC) $(CFLAGS) -c $< -o $@

# Compile transition files
$(OBJ_DIR)/transitions_%.o: $(SRC_DIR)/transitions/%.c | directories protocols
	$(CC) $(CFLAGS) -c $< -o $@

# Link the binary
$(TARGET): $(OBJECTS) $(TRANSITION_OBJECTS) $(PROTO_OBJECTS)
	$(CC) $(OBJECTS) $(TRANSITION_OBJECTS) $(PROTO_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Install
install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/local/bin/$(PROJECT)
	install -Dm644 config/staticwall.vibe $(DESTDIR)/usr/share/$(PROJECT)/config.vibe.example
	install -Dm644 $(ASSETS_DIR)/default.png $(DESTDIR)/usr/share/$(PROJECT)/default.png
	@mkdir -p $(DESTDIR)/usr/share/$(PROJECT)/shaders
	@for shader in examples/shaders/*.glsl; do \
		install -Dm644 $$shader $(DESTDIR)/usr/share/$(PROJECT)/shaders/$$(basename $$shader); \
	done
	install -Dm644 examples/shaders/README.md $(DESTDIR)/usr/share/$(PROJECT)/shaders/README.md
	@echo "Installed to $(DESTDIR)/usr/local/bin/$(PROJECT)"
	@echo "Example config: $(DESTDIR)/usr/share/$(PROJECT)/config.vibe.example"
	@echo "Default wallpaper: $(DESTDIR)/usr/share/$(PROJECT)/default.png"
	@echo "Example shaders: $(DESTDIR)/usr/share/$(PROJECT)/shaders/"
	@echo ""
	@echo "Note: Staticwall runs as normal user. No sudo needed to run!"
	@echo "On first run, config and example shaders will be copied to your home directory"

# Uninstall
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(PROJECT)
	rm -rf $(DESTDIR)/usr/share/$(PROJECT)
	@echo "Uninstalled $(PROJECT)"

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	@echo "Cleaned build directory"

# Clean everything including protocols
distclean: clean
	rm -rf $(PROTO_DIR)/*.c $(PROTO_DIR)/*.h
	@echo "Cleaned all generated files"

# Run the daemon
run: $(TARGET)
	./$(TARGET)

# Debug build
debug: CFLAGS += -g -DDEBUG -O0
debug: clean $(TARGET)

# Help
help:
	@echo "Staticwall - Wayland Wallpaper Daemon"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build the project (default)"
	@echo "  clean       - Remove build artifacts"
	@echo "  distclean   - Remove all generated files"
	@echo "  install     - Install to system (requires sudo)"
	@echo "  uninstall   - Remove from system (requires sudo)"
	@echo "  run         - Build and run"
	@echo "  debug       - Build with debug symbols"
	@echo "  help        - Show this help"
	@echo ""
	@echo "Note: Staticwall runs as a normal user. No root privileges needed!"

.PHONY: all clean distclean install uninstall run debug help directories protocols
