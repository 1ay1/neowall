#!/bin/bash
# Generate Wayland protocol bindings for Staticwall

set -e

PROTO_DIR="protocols"
PROTO_BASE="/usr/share/wayland-protocols"

# Create protocols directory if it doesn't exist
mkdir -p "$PROTO_DIR"

echo "Generating Wayland protocol bindings..."

# wlr-layer-shell
if [ -f "$PROTO_BASE/wlr-unstable/wlr-layer-shell-unstable-v1.xml" ]; then
    echo "  - wlr-layer-shell-unstable-v1"
    wayland-scanner client-header \
        "$PROTO_BASE/wlr-unstable/wlr-layer-shell-unstable-v1.xml" \
        "$PROTO_DIR/wlr-layer-shell-unstable-v1-client-protocol.h"

    wayland-scanner private-code \
        "$PROTO_BASE/wlr-unstable/wlr-layer-shell-unstable-v1.xml" \
        "$PROTO_DIR/wlr-layer-shell-unstable-v1-client-protocol.c"
else
    echo "  ! wlr-layer-shell protocol not found (required)"
    echo "    Install wayland-protocols or wlroots development package"
fi

# xdg-shell
if [ -f "$PROTO_BASE/stable/xdg-shell/xdg-shell.xml" ]; then
    echo "  - xdg-shell"
    wayland-scanner client-header \
        "$PROTO_BASE/stable/xdg-shell/xdg-shell.xml" \
        "$PROTO_DIR/xdg-shell-client-protocol.h"

    wayland-scanner private-code \
        "$PROTO_BASE/stable/xdg-shell/xdg-shell.xml" \
        "$PROTO_DIR/xdg-shell-client-protocol.c"
else
    echo "  ! xdg-shell protocol not found (optional)"
fi

# viewporter
if [ -f "$PROTO_BASE/stable/viewporter/viewporter.xml" ]; then
    echo "  - viewporter"
    wayland-scanner client-header \
        "$PROTO_BASE/stable/viewporter/viewporter.xml" \
        "$PROTO_DIR/viewporter-client-protocol.h"

    wayland-scanner private-code \
        "$PROTO_BASE/stable/viewporter/viewporter.xml" \
        "$PROTO_DIR/viewporter-client-protocol.c"
else
    echo "  ! viewporter protocol not found (optional)"
fi

echo "Protocol generation complete!"
echo ""
echo "Generated files in $PROTO_DIR/:"
ls -lh "$PROTO_DIR/" 2>/dev/null || echo "  (no files generated)"
