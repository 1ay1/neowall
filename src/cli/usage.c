/* --help and --version. */

#include <stdio.h>
#include <string.h>

#include "neowall/cli/cli.h"
#include "neowall/neowall.h"

void print_usage(const char *program_name) {
    printf("NeoWall v%s - GPU-accelerated wallpapers for Wayland. Take the red pill. 🔴\n\n", NEOWALL_VERSION_STRING);
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("       %s set <index>   Set wallpaper by index (0-based)\n", program_name);
    printf("       %s new <name>     Scaffold a new shader you can run immediately\n", program_name);
    printf("                         (--template=scene|flat|reactive, --list)\n");
    printf("       %s import <export.json> [name]\n", program_name);
    printf("                         Turn a Shadertoy export into a neowall shader\n");
    printf("       %s preview <shader.glsl>\n", program_name);
    printf("                         Run one shader in the foreground without\n");
    printf("                         changing your config or stopping the daemon\n");
    printf("       %s watch <shader.glsl>\n", program_name);
    printf("                         Like preview, but recompile on every save and\n");
    printf("                         print errors inline (keeps the last good shader)\n");
    printf("       %s docs [word]\n", program_name);
    printf("                         List the helpers and uniforms shaders can use\n\n");
    printf("Options:\n");
    printf("  -c, --config PATH     Path to configuration file\n");
    printf("  -f, --foreground      Run in foreground (for debugging)\n");
    printf("  -v, --verbose         Enable verbose logging\n");
    printf("      --date OFFSET     Shift the shader clock (+6d, -2h, 90m, 3600)\n");
    printf("      --timelapse SPEC  Compress shader time (SPAN/DURATION, e.g. 30d/20s)\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -V, --version         Show version information\n");
    printf("\n");
    printf("Daemon Control Commands (when daemon is running):\n");
    printf("  kill                  Stop running daemon\n");

    const char *name, *desc;
    for (size_t i = 0; cli_control_command(i, &name, &desc); i++) printf("  %-21s %s\n", name, desc);
    /* Shader-animation freeze controls (handled outside the table — RT signals). */
    printf("  %-21s %s\n", "pause-shader", "Freeze the shader animation in place");
    printf("  %-21s %s\n", "resume-shader", "Resume a frozen shader animation");
    printf("  %-21s %s\n", "ping", "Exit 0 if a daemon is running and answering");
    printf("  %-21s %s\n", "set-terminal <cmd>", "Swap the live terminal-wallpaper command");
    printf("\n");
    printf("Note: By default, neowall runs as a daemon. Use -f for foreground.\n");
    printf("If a daemon is already running, subsequent calls act as control commands.\n");
    printf("\n");
    printf("Configuration file locations (in order of preference):\n");
    printf("  1. $XDG_CONFIG_HOME/neowall/config.vibe\n");
    printf("  2. $HOME/.config/neowall/config.vibe\n");
    printf("  3. %s/neowall/config.vibe\n", NEOWALL_SYSCONFDIR);
    printf("\n");
    printf("Shaders are looked up in ~/.config/neowall/shaders, then\n");
    printf("$XDG_DATA_HOME/neowall/shaders, $XDG_DATA_DIRS/neowall/shaders and %s/shaders.\n", NEOWALL_DATADIR);
    printf("\n");
    printf("Example config.vibe:\n");
    printf("  default {\n");
    printf("    path ~/Pictures/wallpaper.png\n");
    printf("    mode fill\n");
    printf("  }\n");
    printf("\n");
    printf("  output {\n");
    printf("    eDP-1 {\n");
    printf("      path ~/Pictures/laptop-wallpaper.jpg\n");
    printf("      mode fit\n");
    printf("    }\n");
    printf("  }\n");
    printf("\n");
}

void print_version(void) {
    printf("NeoWall v%s\n", NEOWALL_VERSION_STRING);
    printf("GPU-accelerated wallpapers for Wayland.\n");
    printf("Take the red pill. 🔴💊\n");
    printf("\nSupported features:\n");
    printf("  - Live GPU shaders at 60 FPS (Shadertoy compatible)\n");
    printf("  - 2%% CPU usage (lighter than video wallpapers)\n");
    printf("  - Multi-monitor support\n");
    printf("  - Smooth transitions (fade, slide, glitch, pixelate)\n");
    printf("  - Works on Hyprland, Sway, River, and other Wayland compositors\n");
    printf("\nSupported image formats:\n");
    printf("  - PNG\n");
    printf("  - JPEG/JPG\n");
}
