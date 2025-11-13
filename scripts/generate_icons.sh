#!/bin/bash
# Generate PNG icons from SVG sources
# This script converts the SVG icon to multiple PNG sizes for system integration

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SVG_SOURCE="$PROJECT_ROOT/packaging/icon-clean.svg"
ICON_DIR="$PROJECT_ROOT/data/icons"

# Check if source SVG exists
if [ ! -f "$SVG_SOURCE" ]; then
    echo "Error: Source SVG not found at $SVG_SOURCE"
    exit 1
fi

# Create icon directory if it doesn't exist
mkdir -p "$ICON_DIR"

# Check for available conversion tools
if command -v magick >/dev/null 2>&1; then
    CONVERT_CMD="magick"
elif command -v convert >/dev/null 2>&1; then
    CONVERT_CMD="convert"
elif command -v rsvg-convert >/dev/null 2>&1; then
    CONVERT_CMD="rsvg-convert"
elif command -v inkscape >/dev/null 2>&1; then
    CONVERT_CMD="inkscape"
else
    echo "Error: No SVG conversion tool found."
    echo "Please install one of: ImageMagick (magick/convert), librsvg (rsvg-convert), or Inkscape"
    exit 1
fi

echo "Using conversion tool: $CONVERT_CMD"
echo "Generating PNG icons from: $SVG_SOURCE"
echo "Output directory: $ICON_DIR"
echo ""

# Icon sizes for different contexts
# 16, 22, 24, 32, 48, 64, 128, 256
SIZES=(16 22 24 32 48 64 128 256)

# Generate icons
for size in "${SIZES[@]}"; do
    output_file="$ICON_DIR/neowall-icon-${size}.png"
    echo -n "Generating ${size}x${size}... "

    case "$CONVERT_CMD" in
        magick|convert)
            "$CONVERT_CMD" -background transparent "$SVG_SOURCE" -resize "${size}x${size}" "PNG32:$output_file" 2>/dev/null
            ;;
        rsvg-convert)
            rsvg-convert -w "$size" -h "$size" -b none "$SVG_SOURCE" -o "$output_file" 2>/dev/null
            ;;
        inkscape)
            inkscape "$SVG_SOURCE" -w "$size" -h "$size" --export-background-opacity=0 -o "$output_file" 2>/dev/null
            ;;
    esac

    if [ -f "$output_file" ]; then
        file_size=$(du -h "$output_file" | cut -f1)
        echo "✓ ($file_size)"
    else
        echo "✗ Failed"
    fi
done

# Generate default icon (128x128)
echo -n "Generating default icon (128x128)... "
cp "$ICON_DIR/neowall-icon-128.png" "$ICON_DIR/neowall-icon.png"
echo "✓"

echo ""
echo "Icon generation complete!"
echo "Generated files:"
ls -lh "$ICON_DIR"/*.png | awk '{print "  " $9, "(" $5 ")"}'

echo ""
echo "To install icons system-wide, run:"
echo "  sudo meson install -C builddir"
echo ""
echo "For development, icons are automatically found in:"
echo "  $ICON_DIR"
