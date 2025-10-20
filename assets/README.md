# Staticwall Assets

This directory contains default assets for Staticwall.

## Default Wallpaper

The `default.png` file is used as the fallback wallpaper when Staticwall runs for the first time and no configuration exists yet.

### Using Your Own Default Wallpaper

You can replace `default.png` with any image you like. The recommended specifications are:

- **Format**: PNG or JPEG
- **Resolution**: At least 1920x1080 (or your monitor's native resolution)
- **Aspect Ratio**: 16:9 is most common, but any ratio works
- **File Size**: Keep it reasonable (< 5MB recommended)

### Creating a Solid Color Wallpaper

If you want a simple solid color background, you can create one using ImageMagick:

```bash
# Create a 1920x1080 dark gray wallpaper
convert -size 1920x1080 xc:#2e3440 default.png

# Create a 3840x2160 (4K) black wallpaper
convert -size 3840x2160 xc:#000000 default.png
```

Or use any image editor to create a solid color image and save it as `default.png`.