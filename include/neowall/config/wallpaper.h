/* neowall/config/wallpaper.h — what one output should show: the parsed,
 * validated form of a config block. A plain value: no pointers into other
 * modules, no locks. config.c fills it; output and image read it. */
#ifndef NEOWALL_CONFIG_WALLPAPER_H
#define NEOWALL_CONFIG_WALLPAPER_H

#include <stdbool.h>
#include <stddef.h>

#include "neowall/core/constants.h"


/* Wallpaper display modes */
enum wallpaper_mode {
    MODE_CENTER,  /* Center the image without scaling */
    MODE_STRETCH, /* Stretch to fill entire screen */
    MODE_FIT,     /* Scale to fit inside screen, maintain aspect ratio */
    MODE_FILL,    /* Scale to fill screen, maintain aspect ratio, crop if needed */
    MODE_TILE,    /* Tile the image */
};

/* Wallpaper transition types */
enum transition_type {
    TRANSITION_NONE,
    TRANSITION_FADE,
    TRANSITION_SLIDE_LEFT,
    TRANSITION_SLIDE_RIGHT,
    TRANSITION_GLITCH,
    TRANSITION_PIXELATE,
};

/* Wallpaper type */
enum wallpaper_type {
    WALLPAPER_IMAGE,    /* Static image file */
    WALLPAPER_SHADER,   /* Live GLSL shader */
    WALLPAPER_TERMINAL, /* Live terminal (a program rendered as the wallpaper) */
};

/* Wallpaper types that drive the multipass GLSL pipeline and need continuous
 * frame scheduling (frame timer / repeated needs_redraw). Both GLSL shaders and
 * terminal wallpapers render through render_frame_shader(); a static image does
 * not. Kept as one predicate so every animation gate in the render loop and the
 * event loop stays in agreement. */
static inline bool wallpaper_is_animated(enum wallpaper_type t) {
    return t == WALLPAPER_SHADER || t == WALLPAPER_TERMINAL;
}

/* Wallpaper configuration for a specific output */
struct wallpaper_config {
    enum wallpaper_type type;        /* Wallpaper type (image or shader) */
    char path[NW_PATH_MAX];          /* Path to wallpaper image */
    char shader_path[NW_PATH_MAX];   /* Path to GLSL shader file */
    enum wallpaper_mode mode;        /* Display mode */
    float duration;                  /* Duration in seconds (for cycling) */
    enum transition_type transition; /* Transition effect */
    float transition_duration;       /* Transition duration in seconds */
    float shader_speed;              /* Shader animation speed multiplier (default 1.0) */
    int shader_fps;                  /* Target FPS for shader rendering (default 60) */
    float palette[16][3];            /* `palette`: colours for iPalette (0..1 RGB) */
    int palette_count;               /* 0 = none set: shaders use the built-in one */
    bool vsync;               /* Enable vsync (sync to monitor refresh, ignores shader_fps) */
    bool show_fps;            /* Show FPS watermark on screen (default false) */
    bool pause_on_fullscreen; /* Pause rendering when output is occluded by fullscreen window */
    float pause_coverage_threshold; /* Fraction (0.0-1.0) of wallpaper region that must be covered
                                       by tiled windows to count as occluded. Default 0.8 */
    bool span;           /* Explicitly span compatible sources across outputs (default false) */
    char span_group[64]; /* Optional explicit span group name (empty = source-based group) */
    bool cycle;          /* Enable wallpaper cycling */
    bool shuffle;        /* Randomise cycle order on load + on every wrap (issue #47).
                          * Applies to both image and shader directory cycles. */
    char **cycle_paths;  /* Array of paths for cycling */
    size_t cycle_count;  /* Number of wallpapers to cycle */
    size_t current_cycle_index; /* Current index in cycle */

    /* iChannel texture configuration */
    char **channel_paths; /* Array of texture paths/names for iChannels */
    size_t channel_count; /* Number of configured channels */

    /* Terminal wallpaper (WALLPAPER_TERMINAL). The program named by term_cmd is
     * spawned under a PTY and rendered as the background; term_shader (optional)
     * is a GLSL file that samples it via nwTerm() for CRT/glow styling — empty
     * means the built-in crisp pass-through. */
    char term_cmd[NW_PATH_MAX];         /* command to run, e.g. "htop" */
    char term_font[NW_PATH_MAX];        /* font file path ('' = system search) */
    char term_font_bold[NW_PATH_MAX];   /* optional bold face ('' = synth) */
    char term_font_italic[NW_PATH_MAX]; /* optional italic face ('' = synth) */
    char term_cwd[NW_PATH_MAX];         /* working directory ('' = inherit) */
    char term_env[NW_PATH_MAX];         /* TERM value ('' = xterm-256color) */
    int term_cols;                      /* grid width  (0 = auto from output) */
    int term_rows;                      /* grid height (0 = auto from output) */
    int term_font_size;                 /* cell height in px (0 = default 18) */
    long term_fg;                       /* default fg 0xRRGGBB (-1 = built-in) */
    long term_bg;                       /* default bg 0xRRGGBB (-1 = built-in) */
    float term_bloom;                   /* nwTermFX bloom    0..1 (-1 = default) */
    float term_scanline;                /* nwTermFX scanline 0..1 (-1 = default) */
    float term_crt;                     /* nwTermFX CRT curve+vignette 0..1 (-1) */
    float term_chroma;                  /* nwTermFX chromatic aberration 0..1 (-1) */
    float term_fade;                    /* change-driven fade 0..1 (-1 = default) */
};

/* Names for the log and the state file. */
const char *wallpaper_mode_to_string(enum wallpaper_mode mode);
const char *transition_type_to_string(enum transition_type type);

#endif /* NEOWALL_CONFIG_WALLPAPER_H */
