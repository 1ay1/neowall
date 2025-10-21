# Directory Cycling Guide

**The easiest way to rotate through multiple wallpapers in Staticwall**

## 🎯 Quick Start

Instead of manually listing every wallpaper file, just point Staticwall to a folder:

```vibe
default {
  path ~/Pictures/wallpapers/    # ← Directory with trailing slash!
  duration 300                    # Change every 5 minutes
  transition fade
}
```

That's it! Staticwall automatically:
- ✅ Finds all PNG and JPEG files in the directory
- ✅ Loads them in alphabetical order
- ✅ Cycles through them based on `duration`
- ✅ Applies transitions between wallpapers

## 📁 Directory vs File: The Difference

### Directory Mode (with trailing `/`)
```vibe
path ~/Pictures/wallpapers/    # ← DIRECTORY MODE
```
- Loads ALL `.png`, `.jpg`, `.jpeg` files in the folder
- Automatically cycles if `duration` is set
- Scans only the specified directory (not subdirectories)

### Single File Mode (no trailing `/`)
```vibe
path ~/Pictures/wallpaper.png    # ← SINGLE FILE MODE
```
- Loads one specific image
- Stays static (no cycling unless you manually change it)

## ⚠️ Common Mistakes

### Mistake 1: Forgetting the trailing slash
```vibe
# ❌ WRONG - Treated as a single file
path ~/Pictures/wallpapers

# ✅ CORRECT - Treated as a directory
path ~/Pictures/wallpapers/
```

### Mistake 2: Not setting duration
```vibe
# ❌ WRONG - No cycling (duration defaults to 0)
default {
  path ~/Pictures/wallpapers/
}

# ✅ CORRECT - Cycles every 5 minutes
default {
  path ~/Pictures/wallpapers/
  duration 300
}
```

### Mistake 3: Expecting subdirectories to be scanned
```
Pictures/
└── wallpapers/
    ├── image1.png          ← ✅ Will be loaded
    ├── image2.jpg          ← ✅ Will be loaded
    └── nature/
        └── forest.png      ← ❌ Will NOT be loaded (subdirectory)
```

**Solution:** Move all images to the same directory or use multiple configs for different folders.

## 📂 How to Organize Your Wallpapers

### Method 1: Single Directory (Recommended)
```
~/Pictures/wallpapers/
├── 01-mountains.png
├── 02-ocean.jpg
├── 03-forest.png
├── 04-sunset.jpg
└── 05-city.png
```

**Config:**
```vibe
default {
  path ~/Pictures/wallpapers/
  duration 600
}
```

### Method 2: Multiple Directories by Theme
```
~/Pictures/
├── nature/
│   ├── mountains.png
│   └── forest.jpg
├── abstract/
│   ├── colors.png
│   └── patterns.jpg
└── minimal/
    └── dark.png
```

**Config (change path as needed):**
```vibe
# Switch between themes by editing this line
default {
  path ~/Pictures/nature/        # Or ~/Pictures/abstract/, etc.
  duration 300
}
```

### Method 3: Per-Monitor Directories
```
~/Pictures/
├── laptop-walls/
│   └── work-friendly images
└── monitor-walls/
    └── high-res images
```

**Config:**
```vibe
output {
  eDP-1 {
    path ~/Pictures/laptop-walls/
    duration 900
  }
  
  HDMI-A-1 {
    path ~/Pictures/monitor-walls/
    duration 600
  }
}
```

## 🎨 File Naming for Cycling Order

Staticwall cycles through images in **alphabetical order by filename**.

### Pro Tips for Ordering:

**Number prefix (recommended):**
```
01-first.png
02-second.png
03-third.png
10-tenth.png
```

**Date prefix:**
```
2024-01-15-sunrise.jpg
2024-01-16-sunset.jpg
2024-01-17-night.jpg
```

**Zero-padding is important:**
```
# ❌ BAD - Alphabetically: 1, 10, 2, 3
1-image.png
2-image.png
3-image.png
10-image.png

# ✅ GOOD - Alphabetically: 01, 02, 03, 10
01-image.png
02-image.png
03-image.png
10-image.png
```

**Quick rename script:**
```bash
# Rename all images with zero-padded numbers
cd ~/Pictures/wallpapers/
i=1
for file in *.{png,jpg,jpeg}; do
  [ -f "$file" ] && mv "$file" "$(printf "%02d" $i)-$file"
  ((i++))
done
```

## 🔍 Supported File Formats

### ✅ Supported
- `.png` (PNG)
- `.jpg` (JPEG)
- `.jpeg` (JPEG)

**Note:** Extensions are case-insensitive (`.PNG`, `.JPG` work too)

### ❌ Not Supported (Yet)
- `.webp` (WebP)
- `.gif` (GIF)
- `.bmp` (Bitmap)
- `.svg` (SVG)
- `.avif` (AVIF)

## 🚀 Advanced Examples

### Example 1: Different Cycling Speeds
```vibe
default {
  path ~/Pictures/wallpapers/
  duration 300                    # Fast cycling (5 minutes)
  transition fade
  transition_duration 1000        # Slow, smooth transition (1 second)
}
```

### Example 2: Instant Changes (No Transition)
```vibe
default {
  path ~/Pictures/wallpapers/
  duration 60                     # Change every minute
  transition none                 # Instant change, no animation
}
```

### Example 3: Slideshow Mode
```vibe
default {
  path ~/Pictures/slideshow/
  duration 10                     # Change every 10 seconds
  transition slide_left
  transition_duration 500
  mode fill
}
```

### Example 4: Work Hours vs Leisure
```vibe
# Create two configs:

# work.vibe - Minimal, slower cycling
default {
  path ~/Pictures/work-walls/
  duration 3600                   # Once per hour
  transition none
  mode fit
}

# leisure.vibe - Colorful, faster cycling
default {
  path ~/Pictures/fun-walls/
  duration 300                    # Every 5 minutes
  transition fade
  transition_duration 1000
  mode fill
}
```

Switch between them:
```bash
# Work mode
staticwall -c ~/.config/staticwall/work.vibe

# Leisure mode
staticwall kill
staticwall -c ~/.config/staticwall/leisure.vibe
```

## 🐛 Troubleshooting

### Problem: "No images found in directory"

**Check 1:** Verify the directory exists
```bash
ls -la ~/Pictures/wallpapers/
```

**Check 2:** Check for supported image files
```bash
ls ~/Pictures/wallpapers/*.{png,jpg,jpeg}
```

**Check 3:** Check file permissions
```bash
# Make sure you can read the files
chmod 644 ~/Pictures/wallpapers/*
```

**Check 4:** Run with verbose logging
```bash
staticwall -v
```
Look for: `"Loading images from directory"` or error messages

### Problem: "Wallpapers not cycling"

**Checklist:**
- [ ] Path ends with `/` (directory mode enabled)
- [ ] `duration` is set and > 0
- [ ] Directory contains PNG or JPEG files
- [ ] Staticwall is running (not crashed)
- [ ] Run `staticwall -v` to see cycling logs

**Example log output when working:**
```
[2024-10-21 12:00:00] INFO: Loading images from directory: /home/user/Pictures/wallpapers/
[2024-10-21 12:00:00] INFO: Found 12 images in directory
[2024-10-21 12:00:00] INFO: Cycling enabled with duration 300s
[2024-10-21 12:05:00] INFO: Cycling to next wallpaper (2/12)
```

### Problem: "Directory treated as single file"

**Cause:** Missing trailing slash

**Fix:**
```vibe
# ❌ WRONG
path ~/Pictures/wallpapers

# ✅ CORRECT
path ~/Pictures/wallpapers/
```

**Verify:** Run `staticwall -v` and check if it says:
- ✅ `"Loading images from directory"`
- ❌ `"Failed to load image"` (treated as file)

### Problem: "Images cycling in wrong order"

**Cause:** Alphabetical sorting

**Solution:** Rename files with numeric prefixes (see "File Naming" section above)

### Problem: "Subdirectories not being scanned"

**This is intentional.** Staticwall only scans the immediate directory.

**Workaround 1:** Move all images to a single directory
```bash
find ~/Pictures/wallpapers/ -type f \( -name "*.png" -o -name "*.jpg" \) -exec mv {} ~/Pictures/all-walls/ \;
```

**Workaround 2:** Use symlinks
```bash
ln -s ~/Pictures/nature/*.png ~/Pictures/all-walls/
ln -s ~/Pictures/abstract/*.jpg ~/Pictures/all-walls/
```

## 📊 Performance Considerations

### Memory Usage
- Staticwall loads image metadata but only keeps **current wallpaper in memory**
- Large directories (100+ images) are fine
- Each image texture uses VRAM (typically 10-50MB per monitor)

### Directory Scanning
- Directory is scanned **once on startup**
- Adding/removing images requires: `killall -HUP staticwall` (reload config)
- Or use `--watch` flag to auto-reload on config changes

### Recommended Limits
- **Ideal:** 10-50 images per directory
- **Works fine:** 100-200 images
- **May be slow:** 1000+ images (startup time increases)

## 🎓 Best Practices

1. **Organize by theme or mood**
   - Separate directories for work, leisure, seasons, etc.

2. **Use consistent naming**
   - Number prefixes for control over order
   - Descriptive names for easy identification

3. **Optimize image sizes**
   - Match your monitor resolution (e.g., 1920x1080, 3840x2160)
   - Oversized images waste memory

4. **Test your setup**
   - Run `staticwall -v` to verify directory detection
   - Use short duration (e.g., 30s) for testing

5. **Use hot-reload for experimentation**
   ```bash
   staticwall -w    # Daemon with config watching
   # Edit config, changes apply automatically!
   ```

6. **Backup your wallpaper collection**
   - Your curated collection is valuable!
   - Use git, rsync, or cloud storage

## 🔗 Related Documentation

- [Configuration Guide](CONFIG_GUIDE.md) - Complete config reference
- [Quick Start Guide](QUICKSTART.md) - Get started in 5 minutes
- [Troubleshooting](../README.md#troubleshooting) - Common issues

## ❓ FAQ

### Can I mix directories and files?
No, each output/monitor can only have one `path` setting. But you can use different paths for different monitors.

### How do I know which image is currently displayed?
Run `staticwall -v` and watch the logs. It shows cycling messages with image numbers.

### Can I randomize the order?
Not currently. Images always cycle alphabetically. You can rename files to control order.

### Does Staticwall watch the directory for new images?
No. Changes to the directory require a config reload (`killall -HUP staticwall`).

### Can I exclude certain files?
Not directly. Rename them with a different extension (e.g., `.png.bak`) or move them to a subdirectory.

---

**Pro Tip:** Start simple with one directory and `duration 300`, then experiment with transitions and timing once you're comfortable!

**Happy cycling!** 🎨🔄