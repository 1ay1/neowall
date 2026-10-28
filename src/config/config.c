#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <dirent.h>
#include <time.h>
#include "neowall/config/config.h"
#include "neowall/config/vibe.h"
#include "neowall/neowall.h"
#include "neowall/image/image.h"    /* For image_free() */
#include "neowall/config/config_access.h"
#include "neowall/compositor/compositor.h"
#include "neowall/shader/shader.h"

/* ============================================================================
 * CONFIGURATION PHILOSOPHY
 * ============================================================================
 * This config parser is designed to be DETERMINISTIC and UNAMBIGUOUS:
 *
 * 1. IMAGE MODE and SHADER MODE are MUTUALLY EXCLUSIVE
 *    - If both 'path' and 'shader' are specified, it's an ERROR
 *    - No guessing, no precedence rules - just fail validation
 *
 * 2. ALL INPUTS ARE VALIDATED
 *    - Invalid values are rejected with clear error messages
 *    - Missing required fields trigger specific errors
 *
 * 3. FALLBACK TO DEFAULTS
 *    - If config is invalid, use safe built-in defaults
 *    - Never crash, always provide working state
 *
 * 4. EXPLICIT OVER IMPLICIT
 *    - No hidden behaviors or magic conversions
 *    - What you write is exactly what you get
 * ============================================================================ */

/* Get default configuration file path */
const char *config_get_default_path(void) {
    static char path[MAX_PATH_LENGTH];

    /* Try XDG_CONFIG_HOME first */
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home) {
        snprintf(path, sizeof(path), "%s/neowall/config.vibe", xdg_config_home);
        if (access(path, F_OK) == 0) {
            return path;
        }
    }

    /* Try ~/.config */
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/neowall/config.vibe", home);
        if (access(path, F_OK) == 0) {
            return path;
        }
    }

    /* Then the system-wide config ($sysconfdir, /etc on distros) */
    snprintf(path, sizeof(path), "%s/neowall/config.vibe", NEOWALL_SYSCONFDIR);
    if (access(path, F_OK) == 0) {
        return path;
    }

    /* Return user config path even if it doesn't exist */
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/neowall/config.vibe", home);
        return path;
    }

    return NULL;
}

/* ============================================================================
 * Enum String Mapping Tables - Single Source of Truth
 * ============================================================================ */

/* Wallpaper mode mapping table */
typedef struct {
    enum wallpaper_mode mode;
    const char *name;
} WallpaperModeMapping;

static const WallpaperModeMapping mode_mappings[] = {
    {MODE_CENTER,  "center"},
    {MODE_STRETCH, "stretch"},
    {MODE_FIT,     "fit"},
    {MODE_FILL,    "fill"},
    {MODE_TILE,    "tile"},
};

static const size_t mode_mapping_count = sizeof(mode_mappings) / sizeof(mode_mappings[0]);

/* Transition type mapping table */
typedef struct {
    enum transition_type type;
    const char *name;
    const char *alias;
} TransitionMapping;

static const TransitionMapping transition_mappings[] = {
    {TRANSITION_NONE,        "none",        NULL},
    {TRANSITION_FADE,        "fade",        NULL},
    {TRANSITION_SLIDE_LEFT,  "slide-left",  "slide_left"},
    {TRANSITION_SLIDE_RIGHT, "slide-right", "slide_right"},
    {TRANSITION_GLITCH,      "glitch",      NULL},
    {TRANSITION_PIXELATE,    "pixelate",    NULL},
};

static const size_t transition_mapping_count = sizeof(transition_mappings) / sizeof(transition_mappings[0]);

/* ============================================================================
 * String <-> Enum Conversion Functions
 * ============================================================================ */

static bool parse_wallpaper_mode(const char *str, enum wallpaper_mode *mode) {
    if (!str || !mode) return false;
    for (size_t i = 0; i < mode_mapping_count; i++) {
        if (strcasecmp(str, mode_mappings[i].name) == 0) {
            *mode = mode_mappings[i].mode;
            return true;
        }
    }
    return false;
}

enum wallpaper_mode wallpaper_mode_from_string(const char *str) {
    enum wallpaper_mode mode;
    if (parse_wallpaper_mode(str, &mode)) return mode;
    log_error("Invalid wallpaper mode '%s', using 'fill' as default",
              str ? str : "(null)");
    return MODE_FILL;
}

const char *wallpaper_mode_to_string(enum wallpaper_mode mode) {
    for (size_t i = 0; i < mode_mapping_count; i++) {
        if (mode_mappings[i].mode == mode) {
            return mode_mappings[i].name;
        }
    }
    return "fill";  /* Safe default */
}

static bool parse_transition_type(const char *str, enum transition_type *type) {
    if (!str || !type) return false;
    for (size_t i = 0; i < transition_mapping_count; i++) {
        if (strcasecmp(str, transition_mappings[i].name) == 0 ||
            (transition_mappings[i].alias &&
             strcasecmp(str, transition_mappings[i].alias) == 0)) {
            *type = transition_mappings[i].type;
            return true;
        }
    }
    return false;
}

enum transition_type transition_type_from_string(const char *str) {
    enum transition_type type;
    if (parse_transition_type(str, &type)) return type;
    log_error("Invalid transition type '%s', using 'fade' as default",
              str ? str : "(null)");
    return TRANSITION_FADE;
}

const char *transition_type_to_string(enum transition_type type) {
    for (size_t i = 0; i < transition_mapping_count; i++) {
        if (transition_mappings[i].type == type) {
            return transition_mappings[i].name;
        }
    }
    return "fade";  /* Safe default */
}

/* ============================================================================
 * File Type Detection
 * ============================================================================ */

static bool has_extension(const char *filename, const char *ext) {
    if (!filename || !ext) return false;

    size_t name_len = strlen(filename);
    size_t ext_len = strlen(ext);

    if (name_len < ext_len) return false;

    return strcasecmp(filename + name_len - ext_len, ext) == 0;
}

static bool is_image_file(const char *filename) {
    return has_extension(filename, ".png") ||
           has_extension(filename, ".jpg") ||
           has_extension(filename, ".jpeg");
}

static bool is_shader_file(const char *filename) {
    return has_extension(filename, ".glsl") ||
           has_extension(filename, ".frag");
}

/* Comparison function for qsort */
static int compare_strings(const void *a, const void *b) {
    const char *str_a = *(const char **)a;
    const char *str_b = *(const char **)b;
    return strcmp(str_a, str_b);
}

/* Fisher-Yates shuffle on cycle path arrays (issue #47) lives in shuffle.c
 * so it can be unit-tested without the rest of this TU's heavy deps. */

/* ============================================================================
 * Directory Loading Functions
 * ============================================================================ */

/* Levenshtein distance between two short strings, for "did you mean"
 * hints. Strings of 32+ chars count as far apart. */
size_t config_edit_distance(const char *a, const char *b) {
    enum { CAP = 32 };
    size_t la = strlen(a), lb = strlen(b);
    if (la >= CAP || lb >= CAP) return 99;
    size_t prev[CAP + 1], cur[CAP + 1];
    for (size_t j = 0; j <= lb; j++) prev[j] = j;
    for (size_t i = 1; i <= la; i++) {
        cur[0] = i;
        for (size_t j = 1; j <= lb; j++) {
            size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            size_t del = prev[j] + 1, ins = cur[j - 1] + 1, sub = prev[j - 1] + cost;
            cur[j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
        memcpy(prev, cur, sizeof(prev));
    }
    return prev[lb];
}

static void free_path_array(char **paths, size_t count) {
    if (!paths) return;
    for (size_t i = 0; i < count; i++) free(paths[i]);
    free(paths);
}

static bool expand_directory_path(const char *dir_path, char *expanded,
                                  size_t expanded_size) {
    const char *prefix = "";
    const char *suffix = dir_path;
    if (dir_path[0] == '~') {
        if (dir_path[1] != '\0' && dir_path[1] != '/') {
            log_error("Only '~' and '~/' path expansion are supported: %s", dir_path);
            return false;
        }
        prefix = getenv("HOME");
        if (!prefix) {
            log_error("Cannot expand ~ without HOME environment variable");
            return false;
        }
        suffix = dir_path + 1;
    }

    int written = snprintf(expanded, expanded_size, "%s%s", prefix, suffix);
    if (written < 0 || (size_t)written >= expanded_size) {
        log_error("Directory path is too long: %s", dir_path);
        return false;
    }

    size_t len = (size_t)written;
    while (len > 1 && expanded[len - 1] == '/') expanded[--len] = '\0';
    return true;
}

static char **load_files_from_directory(const char *dir_path, size_t *count,
                                        bool (*accept)(const char *)) {
    if (!dir_path || !count) return NULL;
    *count = 0;

    char expanded_path[MAX_PATH_LENGTH];
    if (!expand_directory_path(dir_path, expanded_path, sizeof(expanded_path)))
        return NULL;

    DIR *dir = opendir(expanded_path);
    if (!dir) return NULL;

    char **paths = NULL;
    size_t used = 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if ((entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) ||
            !accept(entry->d_name))
            continue;

        size_t dir_len = strlen(expanded_path);
        size_t name_len = strlen(entry->d_name);
        if (dir_len > SIZE_MAX - name_len - 2 ||
            dir_len + name_len + 2 > MAX_PATH_LENGTH) {
            log_error("Path too long: %s/%s", expanded_path, entry->d_name);
            continue;
        }

        if (used == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 8;
            if (new_capacity < capacity || new_capacity > SIZE_MAX / sizeof(*paths)) {
                free_path_array(paths, used);
                closedir(dir);
                return NULL;
            }
            char **grown = realloc(paths, new_capacity * sizeof(*paths));
            if (!grown) {
                free_path_array(paths, used);
                closedir(dir);
                return NULL;
            }
            paths = grown;
            capacity = new_capacity;
        }

        size_t full_len = dir_len + 1 + name_len;
        paths[used] = malloc(full_len + 1);
        if (!paths[used]) {
            free_path_array(paths, used);
            closedir(dir);
            return NULL;
        }
        memcpy(paths[used], expanded_path, dir_len);
        paths[used][dir_len] = '/';
        memcpy(paths[used] + dir_len + 1, entry->d_name, name_len + 1);
        used++;
    }
    closedir(dir);

    if (used == 0) {
        free(paths);
        return NULL;
    }

    qsort(paths, used, sizeof(*paths), compare_strings);
    *count = used;
    return paths;
}

char **load_shaders_from_directory(const char *dir_path, size_t *count) {
    return load_files_from_directory(dir_path, count, is_shader_file);
}

char **load_images_from_directory(const char *dir_path, size_t *count) {
    return load_files_from_directory(dir_path, count, is_image_file);
}

/* ============================================================================
 * Configuration Validation and Parsing
 * ============================================================================ */

/* Validation result structure */
typedef struct {
    bool valid;
    char error_message[512];
} ValidationResult;

#define VALIDATION_OK() ((ValidationResult){.valid = true, .error_message = ""})
#define VALIDATION_ERROR(fmt, ...) ((ValidationResult){ \
    .valid = false, \
    .error_message = {0} \
}); snprintf(((ValidationResult*)&result)->error_message, 512, fmt, ##__VA_ARGS__)

static ValidationResult validate_path(const char *path) {
    ValidationResult result;

    if (!path || path[0] == '\0') {
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Path is empty or null");
        return result;
    }

    /* Check for valid path characters (basic check) */
    if (strlen(path) >= MAX_PATH_LENGTH) {
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Path too long (max %d chars)", MAX_PATH_LENGTH);
        return result;
    }

    return VALIDATION_OK();
}

static ValidationResult validate_duration(double duration) {
    ValidationResult result;

    if (duration < 0.0) {
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Duration cannot be negative (got %.2f)", duration);
        return result;
    }

    if (duration > 86400.0) {  /* 24 hours in seconds seems reasonable max */
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Duration too large (got %.2f, max 86400.0s)", duration);
        return result;
    }

    return VALIDATION_OK();
}

static ValidationResult validate_shader_speed(double speed) {
    ValidationResult result;

    if (speed <= 0.0) {
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Shader speed must be positive (got %.2f)", speed);
        return result;
    }

    if (speed > 100.0) {  /* Reasonable upper limit */
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Shader speed too large (got %.2f, max 100.0)", speed);
        return result;
    }

    return VALIDATION_OK();
}

static ValidationResult validate_transition_duration(double duration) {
    ValidationResult result;

    if (duration < 0.0) {
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Transition duration cannot be negative (got %.2f)", duration);
        return result;
    }

    if (duration > 10.0) {  /* 10 seconds max seems reasonable */
        result.valid = false;
        snprintf(result.error_message, sizeof(result.error_message),
                "Transition duration too large (got %.2f, max 10.0s)", duration);
        return result;
    }

    return VALIDATION_OK();
}

/* Initialize config with safe defaults */
static void init_wallpaper_config_defaults(struct wallpaper_config *config) {
    config->type = WALLPAPER_IMAGE;
    config->path[0] = '\0';
    config->shader_path[0] = '\0';
    config->mode = MODE_FILL;
    config->duration = 0.0f;  /* No cycling by default */
    config->transition = TRANSITION_FADE;
    config->transition_duration = 0.3f;  /* 0.3 seconds default transition */
    config->shader_speed = 1.0f;
    config->shader_fps = 60;  /* Default 60 FPS for shaders */
    /* Default: vsync ON. A wallpaper has no latency requirement that would
     * justify tearing, and async presentation lets the present rate drift off
     * the display's refresh rate -- motion then jerks visibly even while the
     * FPS counter reads a healthy 60. Set `vsync false` to allow tearing. */
    config->vsync = true;
    config->show_fps = false;  /* Default: no FPS watermark */
    config->pause_on_fullscreen = true;  /* Default: pause rendering when occluded */
    config->pause_coverage_threshold = 0.8f;  /* Default: 80% tiled coverage = occluded */
    config->span = false;
    config->span_group[0] = '\0';
    config->cycle = false;
    config->shuffle = false;
    config->cycle_paths = NULL;
    config->cycle_count = 0;
    config->current_cycle_index = 0;
    config->channel_paths = NULL;
    config->channel_count = 0;
    config->term_cmd[0] = '\0';
    config->term_font[0] = '\0';
    config->term_cols = 0;
    config->term_rows = 0;
    config->term_font_bold[0] = '\0';
    config->term_font_italic[0] = '\0';
    config->term_cwd[0] = '\0';
    config->term_env[0] = '\0';
    config->term_font_size = 0;
    config->term_fg = -1;
    config->term_bg = -1;
    config->term_bloom = -1.0f;
    config->term_scanline = -1.0f;
    config->term_crt = -1.0f;
    config->term_chroma = -1.0f;
    config->term_fade = -1.0f;
}

static bool copy_config_string(char *dst, size_t dst_size, const VibeValue *value,
                               const char *key, const char *context_name,
                               bool allow_empty) {
    if (!value) return true;
    if (value->type != VIBE_TYPE_STRING) {
        log_error("[%s] '%s' must be a string", context_name, key);
        return false;
    }
    size_t len = strlen(value->as_string);
    if ((!allow_empty && len == 0) || len >= dst_size) {
        log_error("[%s] '%s' must be a %sstring shorter than %zu bytes",
                  context_name, key, allow_empty ? "" : "non-empty ", dst_size);
        return false;
    }
    memcpy(dst, value->as_string, len + 1);
    return true;
}

/* Parse a "#RRGGBB" or "RRGGBB" hex colour into 0xRRGGBB, or -1 if invalid. */
static long parse_hex_color(const char *s) {
    if (!s) return -1;
    if (*s == '#') s++;
    long v = 0; int n = 0;
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else return -1;
        v = v * 16 + d; n++;
    }
    return n == 6 ? v : -1;
}

/* Terminal mode: the command plus its term_* options. */
static bool parse_terminal_keys(VibeValue *obj, struct wallpaper_config *config,
                                VibeValue *term_val, const char *context_name) {
    config->type = WALLPAPER_TERMINAL;
    if (!copy_config_string(config->term_cmd, sizeof(config->term_cmd), term_val,
                            "terminal", context_name, false))
        return false;
    log_info("[%s] TERMINAL MODE: command '%s'", context_name, config->term_cmd);

    struct { const char *key; char *dst; size_t size; } strings[] = {
        { "term_font",        config->term_font,        sizeof(config->term_font) },
        { "term_shader",      config->shader_path,      sizeof(config->shader_path) },
        { "term_font_bold",   config->term_font_bold,   sizeof(config->term_font_bold) },
        { "term_font_italic", config->term_font_italic, sizeof(config->term_font_italic) },
        { "term_cwd",         config->term_cwd,         sizeof(config->term_cwd) },
        { "term_env",         config->term_env,         sizeof(config->term_env) },
    };
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        VibeValue *v = vibe_object_get(obj->as_object, strings[i].key);
        if (!copy_config_string(strings[i].dst, strings[i].size, v, strings[i].key,
                                context_name, true))
            return false;
    }

    struct { const char *key; int *dst; int maximum; } ints[] = {
        { "term_cols",      &config->term_cols,      1024 },
        { "term_rows",      &config->term_rows,      512 },
        { "term_font_size", &config->term_font_size, 96 },
    };
    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); i++) {
        VibeValue *v = vibe_object_get(obj->as_object, ints[i].key);
        if (!v) continue;
        if (v->type != VIBE_TYPE_INTEGER || v->as_integer < 0 || v->as_integer > ints[i].maximum) {
            log_error("[%s] '%s' must be an integer between 0 and %d",
                      context_name, ints[i].key, ints[i].maximum);
            return false;
        }
        *ints[i].dst = (int)v->as_integer;
    }

    struct { const char *key; long *dst; } colors[] = {
        { "term_fg", &config->term_fg },
        { "term_bg", &config->term_bg },
    };
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        VibeValue *v = vibe_object_get(obj->as_object, colors[i].key);
        if (!v) continue;
        if (v->type != VIBE_TYPE_STRING || (*colors[i].dst = parse_hex_color(v->as_string)) < 0) {
            log_error("[%s] '%s' must be a six-digit RGB hex string", context_name, colors[i].key);
            return false;
        }
    }

    /* nwTermFX intensities, 0..1. Absent keeps the -1 sentinel, which
     * output_set_terminal maps to the built-in default. */
    struct { const char *key; float *dst; } fx[] = {
        { "term_bloom",    &config->term_bloom },
        { "term_scanline", &config->term_scanline },
        { "term_crt",      &config->term_crt },
        { "term_chroma",   &config->term_chroma },
        { "term_fade",     &config->term_fade },
    };
    for (size_t i = 0; i < sizeof(fx) / sizeof(fx[0]); i++) {
        VibeValue *v = vibe_object_get(obj->as_object, fx[i].key);
        if (!v) continue;
        double f;
        if (v->type == VIBE_TYPE_FLOAT)        f = v->as_float;
        else if (v->type == VIBE_TYPE_INTEGER) f = (double)v->as_integer;
        else {
            log_error("[%s] '%s' must be a number 0..1", context_name, fx[i].key);
            return false;
        }
        *fx[i].dst = (float)(f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f);
        log_info("[%s] %s = %.2f", context_name, fx[i].key, *fx[i].dst);
    }
    return true;
}

/* `channels [a b ...]`: up to four iChannel textures, shaders only. Built in
 * a local array so a bad entry leaves the config untouched. */
static bool parse_channels(VibeValue *obj, struct wallpaper_config *config,
                           const char *context_name) {
    VibeValue *val = vibe_object_get(obj->as_object, "channels");
    if (!val) return true;
    if (val->type != VIBE_TYPE_ARRAY) {
        log_error("[%s] 'channels' must be an array", context_name);
        return false;
    }
    size_t count = val->as_array->count;
    enum { SHADERTOY_CHANNEL_COUNT = 4 };
    if (count > SHADERTOY_CHANNEL_COUNT) {
        log_error("[%s] 'channels' supports at most %d iChannel textures (got %zu)",
                  context_name, SHADERTOY_CHANNEL_COUNT, count);
        return false;
    }
    if (count == 0) return true;
    if (config->type != WALLPAPER_SHADER) {
        log_error("[%s] 'channels' only applies to shaders", context_name);
        return false;
    }

    char **paths = calloc(count, sizeof(char *));
    if (!paths) {
        log_error("[%s] Failed to allocate memory for channels", context_name);
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        VibeValue *e = val->as_array->values[i];
        if (!e || e->type != VIBE_TYPE_STRING || e->as_string[0] == '\0' ||
            strlen(e->as_string) >= MAX_PATH_LENGTH) {
            log_error("[%s] Channel[%zu] must be a non-empty string shorter than %d bytes",
                      context_name, i, MAX_PATH_LENGTH);
            free_path_array(paths, i);
            return false;
        }
        paths[i] = strdup(e->as_string);
        if (!paths[i]) {
            free_path_array(paths, i);
            return false;
        }
        log_debug("[%s] iChannel%zu: %s", context_name, i, paths[i]);
    }
    config->channel_paths = paths;
    config->channel_count = count;
    log_info("[%s] Loaded %zu iChannel texture assignments", context_name, count);
    return true;
}

/* An image or shader source: one file, or a directory whose files become a
 * cycle. Shuffling happens here, before the per-output copy, so every output
 * sharing this block sees the same order (issue #47). */
static bool parse_cycle_source(struct wallpaper_config *config, const char *src,
                               bool shader, const char *context_name) {
    const char *kind = shader ? "SHADER" : "IMAGE";
    ValidationResult v = validate_path(src);
    if (!v.valid) {
        log_error("[%s] Invalid %s path: %s", context_name, shader ? "shader" : "image",
                  v.error_message);
        return false;
    }
    config->type = shader ? WALLPAPER_SHADER : WALLPAPER_IMAGE;
    char *dst = shader ? config->shader_path : config->path;
    size_t dst_size = shader ? sizeof(config->shader_path) : sizeof(config->path);

    size_t len = strlen(src);
    bool is_dir_syntax = len > 0 && src[len - 1] == '/';
    size_t count = 0;
    char **paths = shader ? load_shaders_from_directory(src, &count)
                          : load_images_from_directory(src, &count);

    if (paths && count > 0) {
        config->cycle = true;
        config->cycle_count = count;
        config->cycle_paths = paths;
        config->current_cycle_index = 0;   /* restored per output later */
        if (config->shuffle) {
            config_shuffle_cycle_paths(config->cycle_paths, config->cycle_count, false);
            log_info("[%s] %s MODE: shuffled %zu-entry cycle", context_name, kind, count);
        }
        snprintf(dst, dst_size, "%s", paths[0]);
        log_info("[%s] %s MODE: %zu files from directory for cycling", context_name, kind, count);
        return true;
    }
    free(paths);
    if (is_dir_syntax) {
        log_error("[%s] '%s' ends with '/' but has no %s files", context_name, src,
                  shader ? "shader" : "image");
        return false;
    }
    snprintf(dst, dst_size, "%s", src);
    log_info("[%s] %s MODE: single file '%s'", context_name, kind, src);
    return true;
}

/* Parse wallpaper configuration with strict validation */
static bool parse_wallpaper_config(VibeValue *obj, struct wallpaper_config *config,
                                   const char *context_name) {
    if (!obj || !config || obj->type != VIBE_TYPE_OBJECT) {
        log_error("[%s] Invalid parameters for parse_wallpaper_config", context_name);
        return false;
    }

    /* Initialize with safe defaults */
    init_wallpaper_config_defaults(config);

    /* Parse 'shuffle' EARLY — needs to be in scope before the directory
     * branches below so we can apply it the moment cycle_paths is built. */
    VibeValue *shuffle_val = vibe_object_get(obj->as_object, "shuffle");
    if (shuffle_val) {
        if (shuffle_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("[%s] 'shuffle' must be a boolean (true or false), got type: %d",
                     context_name, shuffle_val->type);
            return false;
        }
        config->shuffle = shuffle_val->as_boolean;
    }

    VibeValue *span_val = vibe_object_get(obj->as_object, "span");
    if (span_val) {
        if (span_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("[%s] 'span' must be a boolean (true or false)", context_name);
            return false;
        }
        config->span = span_val->as_boolean;
    }
    VibeValue *span_group_val = vibe_object_get(obj->as_object, "span_group");
    if (!copy_config_string(config->span_group, sizeof(config->span_group), span_group_val,
                            "span_group", context_name, false)) {
        return false;
    }
    /* A named group is itself an explicit opt-in; accepting it without a
     * redundant span=true keeps per-output configuration concise. */
    if (span_group_val) config->span = true;

    /* Check for 'path' and 'shader' - these are MUTUALLY EXCLUSIVE */
    VibeValue *path_val = vibe_object_get(obj->as_object, "path");
    VibeValue *shader_val = vibe_object_get(obj->as_object, "shader");
    VibeValue *term_val = vibe_object_get(obj->as_object, "terminal");

    bool has_path = path_val && path_val->type == VIBE_TYPE_STRING;
    bool has_shader = shader_val && shader_val->type == VIBE_TYPE_STRING;
    bool has_terminal = term_val && term_val->type == VIBE_TYPE_STRING;

    if ((path_val && !has_path) || (shader_val && !has_shader) ||
        (term_val && !has_terminal)) {
        log_error("[%s] 'path', 'shader', and 'terminal' must be strings", context_name);
        return false;
    }

    /* RULE: path, shader and terminal are mutually exclusive */
    if ((has_path && has_shader) || (has_path && has_terminal) ||
        (has_shader && has_terminal)) {
        log_error("[%s] INVALID CONFIG: 'path', 'shader' and 'terminal' are "
                 "mutually exclusive; specify exactly one.", context_name);
        return false;
    }

    /* RULE: At least one must be specified */
    if (!has_path && !has_shader && !has_terminal) {
        log_error("[%s] INVALID CONFIG: none of 'path', 'shader', 'terminal' "
                 "specified. You must specify exactly one.", context_name);
        return false;
    }

    /* ========================================================================
     * IMAGE / SHADER MODE: a file, or a directory to cycle through
     * ======================================================================== */
    if (has_path && !parse_cycle_source(config, path_val->as_string, false, context_name)) {
        return false;
    }
    if (has_shader && !parse_cycle_source(config, shader_val->as_string, true, context_name)) {
        return false;
    }

    /* ========================================================================
     * TERMINAL MODE: 'terminal' names the command to run as the wallpaper.
     * Optional: term_font (path), term_cols/term_rows (grid), term_shader
     * (a GLSL styling pass that samples nwTerm()).
     * ======================================================================== */
    if (has_terminal && !parse_terminal_keys(obj, config, term_val, context_name)) {
        return false;
    }

    /* ========================================================================
     * Parse optional parameters (valid for both modes)
     * ======================================================================== */

    /* Parse mode */
    VibeValue *mode_val = vibe_object_get(obj->as_object, "mode");
    if (mode_val) {
        if (mode_val->type != VIBE_TYPE_STRING) {
            log_error("[%s] 'mode' must be a string", context_name);
            return false;
        }

        if (config->type == WALLPAPER_SHADER) {
            log_error("[%s] INVALID CONFIG: 'mode' specified in SHADER mode. "
                     "Display modes (fill, fit, center, etc.) only apply to image wallpapers. "
                     "Shaders always render fullscreen.", context_name);
            return false;
        }

        if (!parse_wallpaper_mode(mode_val->as_string, &config->mode)) {
            log_error("[%s] Invalid wallpaper mode '%s'", context_name,
                      mode_val->as_string);
            return false;
        }
    }

    /* Parse duration (for cycling) */
    VibeValue *duration_val = vibe_object_get(obj->as_object, "duration");
    if (duration_val) {
        double duration_value = 0.0;

        if (duration_val->type == VIBE_TYPE_FLOAT) {
            duration_value = duration_val->as_float;
        } else if (duration_val->type == VIBE_TYPE_INTEGER) {
            duration_value = (double)duration_val->as_integer;
        } else {
            log_error("[%s] 'duration' must be a number (seconds)", context_name);
            return false;
        }

        ValidationResult dur_validation = validate_duration(duration_value);
        if (!dur_validation.valid) {
            log_error("[%s] Invalid duration: %s", context_name,
                     dur_validation.error_message);
            return false;
        }

        config->duration = (float)duration_value;

        if (config->duration > 0.0f && !config->cycle) {
            log_info("[%s] Duration specified but no cycling enabled (single file). "
                    "Duration will have no effect.", context_name);
        }

        log_info("[%s] Duration set to: %.2f seconds", context_name, config->duration);
    }

    /* Parse transition */
    VibeValue *transition_val = vibe_object_get(obj->as_object, "transition");
    if (transition_val) {
        if (transition_val->type != VIBE_TYPE_STRING) {
            log_error("[%s] 'transition' must be a string", context_name);
            return false;
        }

        if (!parse_transition_type(transition_val->as_string, &config->transition)) {
            log_error("[%s] Invalid transition type '%s'", context_name,
                      transition_val->as_string);
            return false;
        }

        if (config->type == WALLPAPER_SHADER) {
            log_error("[%s] INVALID CONFIG: 'transition' specified in SHADER mode. "
                     "Transitions only apply to image wallpapers. This setting is invalid for shaders.",
                     context_name);
            return false;
        }

        log_info("[%s] Transition set to: %s (type=%d)", context_name,
                 transition_val->as_string, config->transition);
    }
    /* Parse transition_duration */
    VibeValue *trans_dur_val = vibe_object_get(obj->as_object, "transition_duration");
    if (trans_dur_val) {
        double trans_duration_value = 0.0;

        if (trans_dur_val->type == VIBE_TYPE_FLOAT) {
            trans_duration_value = trans_dur_val->as_float;
        } else if (trans_dur_val->type == VIBE_TYPE_INTEGER) {
            trans_duration_value = (double)trans_dur_val->as_integer;
        } else {
            log_error("[%s] 'transition_duration' must be a number (seconds)",
                     context_name);
            return false;
        }

        ValidationResult trans_dur_validation =
            validate_transition_duration(trans_duration_value);
        if (!trans_dur_validation.valid) {
            log_error("[%s] Invalid transition_duration: %s", context_name,
                     trans_dur_validation.error_message);
            return false;
        }

        config->transition_duration = (float)trans_duration_value;

        if (config->type == WALLPAPER_SHADER) {
            log_error("[%s] INVALID CONFIG: 'transition_duration' specified in SHADER mode. "
                     "Transitions only apply to image wallpapers. This setting is invalid for shaders.",
                     context_name);
            return false;
        }

        log_info("[%s] Transition duration set to: %.2f seconds", context_name, config->transition_duration);
    }

    /* Parse shader_speed (only relevant for shader mode) */
    VibeValue *shader_speed_val = vibe_object_get(obj->as_object, "shader_speed");
    if (shader_speed_val) {
        double speed = 0.0;

        if (shader_speed_val->type == VIBE_TYPE_FLOAT) {
            speed = shader_speed_val->as_float;
        } else if (shader_speed_val->type == VIBE_TYPE_INTEGER) {
            speed = (double)shader_speed_val->as_integer;
        } else {
            log_error("[%s] 'shader_speed' must be a number", context_name);
            return false;
        }

        ValidationResult speed_validation = validate_shader_speed(speed);
        if (!speed_validation.valid) {
            log_error("[%s] Invalid shader_speed: %s", context_name,
                     speed_validation.error_message);
            return false;
        }

        config->shader_speed = (float)speed;

        if (config->type != WALLPAPER_SHADER) {
            log_error("[%s] INVALID CONFIG: 'shader_speed' specified in IMAGE mode. "
                     "Shader speed only applies to GLSL shaders. This setting is invalid for images.",
                     context_name);
            return false;
        }
    }

    /* Parse shader_fps (only relevant for shader mode) */
    VibeValue *shader_fps_val = vibe_object_get(obj->as_object, "shader_fps");
    if (shader_fps_val) {
        double fps_value;
        if (shader_fps_val->type == VIBE_TYPE_INTEGER) {
            if (shader_fps_val->as_integer < 1 || shader_fps_val->as_integer > 240) {
                log_error("[%s] Invalid shader_fps: value must be between 1 and 240",
                          context_name);
                return false;
            }
            fps_value = (double)shader_fps_val->as_integer;
        } else if (shader_fps_val->type == VIBE_TYPE_FLOAT) {
            fps_value = shader_fps_val->as_float;
            if (!isfinite(fps_value) || fps_value < 1.0 || fps_value > 240.0 ||
                trunc(fps_value) != fps_value) {
                log_error("[%s] Invalid shader_fps: value must be a whole number between 1 and 240",
                          context_name);
                return false;
            }
        } else {
            log_error("[%s] 'shader_fps' must be a number", context_name);
            return false;
        }

        int fps = (int)fps_value;

        config->shader_fps = fps;
        log_info("[%s] Shader FPS set to: %d", context_name, fps);

        if (config->type != WALLPAPER_SHADER && config->type != WALLPAPER_TERMINAL) {
            log_error("[%s] INVALID CONFIG: 'shader_fps' specified in IMAGE mode. "
                     "Shader FPS only applies to GLSL shaders. This setting is invalid for images.",
                     context_name);
            return false;
        }
    }

    /* Parse vsync (only relevant for shader mode) */
    VibeValue *vsync_val = vibe_object_get(obj->as_object, "vsync");
    if (vsync_val) {
        if (vsync_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("[%s] 'vsync' must be a boolean (true or false), got type: %d",
                     context_name, vsync_val->type);
            return false;
        }
        config->vsync = vsync_val->as_boolean;

        if (config->vsync) {
            log_info("[%s] Vsync: ENABLED (will sync to monitor refresh rate, shader_fps ignored)", context_name);
        } else {
            log_info("[%s] Vsync: disabled (using custom FPS with tearing control)", context_name);
        }

        if (config->type != WALLPAPER_SHADER && config->type != WALLPAPER_TERMINAL) {
            log_error("[%s] INVALID CONFIG: 'vsync' specified in IMAGE mode. "
                     "Vsync only applies to GLSL shaders. This setting is invalid for images.",
                     context_name);
            return false;
        }
    }

    /* Parse show_fps */
    VibeValue *show_fps_val = vibe_object_get(obj->as_object, "show_fps");
    if (show_fps_val) {
        if (show_fps_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("[%s] 'show_fps' must be a boolean (true or false), got type: %d",
                     context_name, show_fps_val->type);
            log_error("[%s] Invalid value for show_fps - use 'true' or 'false' only", context_name);
            log_error("[%s] Config parsing failed due to invalid show_fps value", context_name);
            return false;
        }
        config->show_fps = show_fps_val->as_boolean;
        log_info("[%s] FPS watermark: %s", context_name, config->show_fps ? "enabled" : "disabled");
    }

    /* Parse pause_on_fullscreen */
    VibeValue *pause_fs_val = vibe_object_get(obj->as_object, "pause_on_fullscreen");
    if (pause_fs_val) {
        if (pause_fs_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("[%s] 'pause_on_fullscreen' must be a boolean (true or false), got type: %d",
                     context_name, pause_fs_val->type);
            return false;
        }
        config->pause_on_fullscreen = pause_fs_val->as_boolean;
        log_info("[%s] Pause on fullscreen: %s", context_name,
                 config->pause_on_fullscreen ? "enabled" : "disabled");
    }

    /* Parse pause_coverage_threshold */
    VibeValue *pause_cov_val = vibe_object_get(obj->as_object, "pause_coverage_threshold");
    if (pause_cov_val) {
        double cov = 0.0;
        if (pause_cov_val->type == VIBE_TYPE_FLOAT) {
            cov = pause_cov_val->as_float;
        } else if (pause_cov_val->type == VIBE_TYPE_INTEGER) {
            cov = (double)pause_cov_val->as_integer;
        } else {
            log_error("[%s] 'pause_coverage_threshold' must be a number between 0.0 and 1.0, got type: %d",
                     context_name, pause_cov_val->type);
            return false;
        }
        if (cov < 0.0 || cov > 1.0) {
            log_error("[%s] 'pause_coverage_threshold' must be between 0.0 and 1.0, got: %.3f",
                     context_name, cov);
            return false;
        }
        config->pause_coverage_threshold = (float)cov;
        log_info("[%s] Pause coverage threshold: %.2f (%.0f%%)", context_name,
                 config->pause_coverage_threshold, config->pause_coverage_threshold * 100.0f);
        /* This setting only affects Hyprland's tiled-coverage path. On every
         * other compositor it's silently inert — warn the user once so they
         * don't think the wallpaper is ignoring them. We detect Hyprland via
         * its IPC env var, the same signal hyprland_coverage_available() uses
         * at runtime, so this stays accurate even when the binary was built
         * with both backends. */
        if (!getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
            log_info("[%s] Note: pause_coverage_threshold only affects Hyprland's "
                     "tiled-coverage detection. On other compositors it has no "
                     "effect (other backends pause via fullscreen/maximized signals "
                     "or the frame-callback watchdog).", context_name);
        }
    }

    if (!parse_channels(obj, config, context_name)) return false;

    /* Unknown keys are warned about, not rejected, so a config written for a
     * newer neowall still loads. Suggest the closest key to catch typos. */
    static const char *const known_keys[] = {
        "path", "shader", "terminal", "term_font", "term_cols", "term_rows",
        "term_shader", "term_font_bold", "term_font_italic", "term_cwd",
        "term_env", "term_font_size", "term_fg", "term_bg",
        "term_bloom", "term_scanline", "term_crt", "term_chroma", "term_fade",
        "mode", "duration", "transition",
        "transition_duration", "shader_speed", "channels", "shader_fps", "vsync", "show_fps",
        "pause_on_fullscreen", "pause_coverage_threshold", "shuffle",
        "span", "span_group",
    };
    size_t known_key_count = sizeof(known_keys) / sizeof(known_keys[0]);

    for (size_t i = 0; i < obj->as_object->count; i++) {
        const char *key = obj->as_object->entries[i].key;
        const char *closest = NULL;
        size_t best = (size_t)-1;
        bool is_known = false;

        for (size_t j = 0; j < known_key_count; j++) {
            if (strcmp(key, known_keys[j]) == 0) {
                is_known = true;
                break;
            }
            size_t d = config_edit_distance(key, known_keys[j]);
            if (d < best) {
                best = d;
                closest = known_keys[j];
            }
        }

        if (is_known) continue;
        if (closest && best <= 2) {
            log_warn("[%s] Unknown key '%s', ignored. Did you mean '%s'?",
                     context_name, key, closest);
        } else {
            log_warn("[%s] Unknown key '%s', ignored", context_name, key);
        }
    }

    return true;
}

/* Free wallpaper configuration */
void config_free_wallpaper(struct wallpaper_config *config) {
    if (!config) return;

    /* Free cycle paths */
    if (config->cycle_paths) {
        for (size_t i = 0; i < config->cycle_count; i++) {
            free(config->cycle_paths[i]);
        }
        free(config->cycle_paths);
        config->cycle_paths = NULL;
    }

    /* Free channel paths */
    if (config->channel_paths) {
        for (size_t i = 0; i < config->channel_count; i++) {
            free(config->channel_paths[i]);
        }
        free(config->channel_paths);
        config->channel_paths = NULL;
    }

    config->cycle_count = 0;
    config->channel_count = 0;
}

/* ============================================================================
 * Default Configuration Creation
 * ============================================================================ */

/* mkdir -p, 0755. */
static bool mkdir_p(const char *path) {
    char tmp[MAX_PATH_LENGTH];
    int n = snprintf(tmp, sizeof(tmp), "%s", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return false;
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
        *p = '/';
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

/* Copy src to dst via a temp file + rename, so a crash never leaves a
 * half-written config. Refuses to overwrite an existing dst. */
static bool copy_file(const char *src, const char *dst) {
    if (access(dst, F_OK) == 0) return false;
    char tmp[MAX_PATH_LENGTH];
    int n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", dst);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return false;

    FILE *in = fopen(src, "rb");
    if (!in) return false;
    int fd = mkstemp(tmp);
    if (fd < 0) { fclose(in); return false; }
    FILE *out = fdopen(fd, "wb");
    if (!out) { close(fd); unlink(tmp); fclose(in); return false; }

    char buf[8192];
    size_t got;
    bool ok = true;
    while (ok && (got = fread(buf, 1, sizeof(buf), in)) > 0) {
        ok = fwrite(buf, 1, got, out) == got;
    }
    ok = ok && !ferror(in);
    fclose(in);
    ok = (fflush(out) == 0) && ok;
    ok = (fsync(fileno(out)) == 0) && ok;
    ok = (fclose(out) == 0) && ok;
    if (ok) ok = chmod(tmp, 0644) == 0 && rename(tmp, dst) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

/* Copy every *.glsl / *.neowall / README.md from one installed shader dir. */
static bool copy_shaders_from(const char *datadir, void *vdst) {
    const char *dst_dir = vdst;
    char src_dir[MAX_PATH_LENGTH];
    int n = snprintf(src_dir, sizeof(src_dir), "%s/shaders", datadir);
    if (n <= 0 || (size_t)n >= sizeof(src_dir)) return false;
    DIR *dir = opendir(src_dir);
    if (!dir) return false;

    int copied = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        size_t len = strlen(e->d_name);
        bool want = (len > 5 && strcmp(e->d_name + len - 5, ".glsl") == 0) ||
                    (len > 8 && strcmp(e->d_name + len - 8, ".neowall") == 0) ||
                    strcmp(e->d_name, "README.md") == 0;
        if (!want) continue;
        char s[MAX_PATH_LENGTH], d[MAX_PATH_LENGTH];
        int a = snprintf(s, sizeof(s), "%s/%s", src_dir, e->d_name);
        int b = snprintf(d, sizeof(d), "%s/%s", dst_dir, e->d_name);
        if (a <= 0 || (size_t)a >= sizeof(s) || b <= 0 || (size_t)b >= sizeof(d)) continue;
        if (copy_file(s, d)) copied++;
    }
    closedir(dir);
    if (copied > 0) log_info("Copied %d example shader(s) to %s", copied, dst_dir);
    return true;   /* only take shaders from the first data dir that has them */
}

static bool config_create_default(const char *config_path) {
    if (!config_path) return false;

    char dir_path[MAX_PATH_LENGTH];
    int n = snprintf(dir_path, sizeof(dir_path), "%s", config_path);
    if (n <= 0 || (size_t)n >= sizeof(dir_path)) return false;
    char *slash = strrchr(dir_path, '/');
    if (slash && slash != dir_path) *slash = '\0';
    if (!mkdir_p(dir_path)) {
        log_error("Failed to create directory %s: %s", dir_path, strerror(errno));
        return false;
    }

    /* The sample configs and shaders ship in neowall's data dir. */
    char src[MAX_PATH_LENGTH];
    bool copied_config = neowall_find_data("config.vibe", src, sizeof(src)) &&
                         copy_file(src, config_path);
    if (copied_config) log_info("Created configuration file from example: %s", config_path);

    char path[MAX_PATH_LENGTH];
    n = snprintf(path, sizeof(path), "%s/neowall.vibe", dir_path);
    if (n > 0 && (size_t)n < sizeof(path) &&
        neowall_find_data("neowall.vibe", src, sizeof(src)) && copy_file(src, path)) {
        log_info("Copied the commented example config to %s", path);
    }

    n = snprintf(path, sizeof(path), "%s/shaders", dir_path);
    if (n > 0 && (size_t)n < sizeof(path) && mkdir_p(path)) {
        neowall_each_data_dir(copy_shaders_from, path);
    }

    if (copied_config) {
        log_info("Edit %s to customize your wallpaper setup", config_path);
        return true;
    }

    /* Nothing installed (running from a build tree): write a minimal config
     * that points at a shader we know works without any files. */
    log_info("No example config installed, writing a minimal one");
    static const char fallback_config[] =
        "# NeoWall configuration. See neowall.vibe next to this file, or\n"
        "# `man neowall.vibe`, for every option.\n"
        "#\n"
        "# Image:      default { path ~/Pictures/wallpaper.png  mode fill }\n"
        "# Slideshow:  default { path ~/Pictures/Wallpapers/  duration 300 }\n"
        "# Shader:     default { shader garden.glsl }\n"
        "\n"
        "default {\n"
        "  path ~/Pictures/wallpaper.png\n"
        "  mode fill\n"
        "}\n";
    char tmp[MAX_PATH_LENGTH];
    n = snprintf(tmp, sizeof(tmp), "%s.XXXXXX", config_path);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return false;
    int fd = mkstemp(tmp);
    if (fd < 0) {
        log_error("Failed to create default config file: %s", strerror(errno));
        return false;
    }
    size_t len = sizeof(fallback_config) - 1;
    bool ok = write(fd, fallback_config, len) == (ssize_t)len && fsync(fd) == 0;
    ok = (close(fd) == 0) && ok;
    ok = ok && chmod(tmp, 0644) == 0 && rename(tmp, config_path) == 0;
    if (!ok) {
        unlink(tmp);
        log_error("Failed to write default config file %s", config_path);
        return false;
    }
    log_info("Created minimal configuration file: %s", config_path);
    return true;
}

/* ============================================================================
 * Built-in fallback configuration (used when config file fails)
 * ============================================================================ */

static bool apply_builtin_default_config(struct neowall_state *state) {
    /* config_load() forwards a NULL state straight here on its early-out path,
     * so guard before dereferencing state->output_list_lock below. */
    if (!state) {
        log_error("Cannot apply built-in default config: state is NULL");
        return false;
    }

    log_info("Applying built-in default configuration");

    /* Create a minimal working config */
    struct wallpaper_config default_config;
    init_wallpaper_config_defaults(&default_config);

    /* Try to find a reasonable default image */
    const char *home = getenv("HOME");
    if (home) {
        /* Try common wallpaper locations */
        const char *try_paths[] = {
            "~/.local/share/neowall/default.png",
            "~/Pictures/wallpaper.png",
            "~/Pictures/wallpapers/wallpaper.png",
            "~/Pictures/WallpaperBank/",
            "~/Pictures/Wallpapers/",
            "~/Pictures/",
            "/usr/share/backgrounds/default.png",
            "/usr/share/backgrounds/",
            "/usr/share/pixmaps/",
            NULL
        };

        for (int i = 0; try_paths[i] != NULL; i++) {
            char expanded[MAX_PATH_LENGTH];
            if (try_paths[i][0] == '~') {
                snprintf(expanded, sizeof(expanded), "%s%s", home, try_paths[i] + 1);
            } else {
                snprintf(expanded, sizeof(expanded), "%s", try_paths[i]);
            }

            /* Check if it's a file */
            struct stat st;
            if (stat(expanded, &st) == 0 && S_ISREG(st.st_mode) && access(expanded, R_OK) == 0) {
                snprintf(default_config.path, sizeof(default_config.path), "%s", expanded);
                log_info("Using default wallpaper: %s", expanded);
                break;
            }

            /* Check if it's a directory with images */
            if (stat(expanded, &st) == 0 && S_ISDIR(st.st_mode)) {
                size_t count = 0;
                char **images = load_images_from_directory(expanded, &count);
                if (images && count > 0) {
                    snprintf(default_config.path, sizeof(default_config.path), "%s", images[0]);
                    log_info("Using default wallpaper from directory: %s", default_config.path);
                    /* Free the list */
                    for (size_t j = 0; j < count; j++) {
                        free(images[j]);
                    }
                    free(images);
                    break;
                }
                if (images) {
                    free(images);
                }
            }
        }
    }

    if (default_config.path[0] == '\0') {
        log_error("No default wallpaper found in common locations.");
        log_error("Please create a config file with a valid 'path' or 'shader' setting.");
        log_info("Continuing without wallpaper - outputs will show black screen.");
        /* Don't return false - we'll just have black screens which is acceptable */
    }

    /* Apply to all outputs - even if no image found, outputs need valid config
     * If no image was found, the config will have an empty path and outputs
     * will render black (which is better than crashing or having invalid state) */
    /* Use read lock for safe output list traversal */
    pthread_rwlock_rdlock(&state->output_list_lock);
    struct output_state *output = state->outputs;
    while (output) {
        struct wallpaper_config config_copy;
        memcpy(&config_copy, &default_config, sizeof(struct wallpaper_config));
        output_apply_config(output, &config_copy);
        log_debug("Applied default config to output %s",
                 output->model[0] ? output->model : "unknown");
        output = output->next;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    return true;
}

/* ============================================================================
 * Main Configuration Loading Function
 * ============================================================================ */

/* Read + parse the config file into a VibeValue tree. On success returns the
 * root object and hands ownership of the parser back via *out_parser (both must
 * be freed by the caller: vibe_value_free(root) then vibe_parser_free(parser)).
 * Returns NULL on any read/parse error (errors are logged). */
static VibeValue *read_and_parse_config(const char *config_path,
                                        VibeParser **out_parser) {
    *out_parser = NULL;

    struct stat st;
    if (stat(config_path, &st) == -1 || !S_ISREG(st.st_mode) ||
        st.st_size > 1024 * 1024) {
        return NULL;
    }

    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        return NULL;
    }
    char *content = malloc(st.st_size + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }
    size_t bytes_read = fread(content, 1, st.st_size, fp);
    content[bytes_read] = '\0';
    fclose(fp);
    if (bytes_read != (size_t)st.st_size) {
        free(content);
        return NULL;
    }

    VibeParser *parser = vibe_parser_new();
    if (!parser) {
        free(content);
        return NULL;
    }
    VibeValue *root = vibe_parse_string(parser, content);
    free(content);

    if (!root || root->type != VIBE_TYPE_OBJECT) {
        if (root) vibe_value_free(root);
        vibe_parser_free(parser);
        return NULL;
    }

    *out_parser = parser;
    return root;
}

/* Find the config block that applies to `output`, honoring default ->
 * output-specific precedence, parse it, and apply it to that single output.
 * The output is matched by connector name first (e.g. DP-1), then model name. */
bool config_apply_to_output(struct neowall_state *state, struct output_state *output) {
    if (!state || !output) {
        return false;
    }

    const char *name = output->connector_name[0] ? output->connector_name
                                                 : output->model;

    VibeParser *parser = NULL;
    VibeValue *root = read_and_parse_config(state->config_path, &parser);
    if (!root) {
        log_error("Reconnect: could not read config for output %s, using built-in default",
                  name[0] ? name : "unknown");
        struct wallpaper_config def;
        init_wallpaper_config_defaults(&def);
        return output_apply_config(output, &def);
    }

    /* Start from the default block (if any), then let an output-specific block
     * override it — exactly the precedence config_load() uses. */
    struct wallpaper_config merged;
    bool have_config = false;

    VibeValue *default_obj = vibe_object_get(root->as_object, "default");
    if (default_obj && default_obj->type == VIBE_TYPE_OBJECT) {
        if (parse_wallpaper_config(default_obj, &merged, "default")) {
            have_config = true;
        }
    }

    VibeValue *outputs_obj = vibe_object_get(root->as_object, "output");
    if (!outputs_obj) {
        outputs_obj = vibe_object_get(root->as_object, "outputs");
    }
    if (outputs_obj && outputs_obj->type == VIBE_TYPE_OBJECT) {
        for (size_t i = 0; i < outputs_obj->as_object->count; i++) {
            const char *key = outputs_obj->as_object->entries[i].key;
            VibeValue *blk = outputs_obj->as_object->entries[i].value;
            if (blk->type != VIBE_TYPE_OBJECT) {
                continue;
            }
            bool matches =
                (output->connector_name[0] && strcmp(output->connector_name, key) == 0) ||
                strcmp(output->model, key) == 0;
            if (!matches) {
                continue;
            }
            char ctx[128];
            snprintf(ctx, sizeof(ctx), "output.%s", key);
            struct wallpaper_config specific;
            if (parse_wallpaper_config(blk, &specific, ctx)) {
                if (have_config) {
                    config_free_wallpaper(&merged);
                }
                merged = specific;
                have_config = true;
            }
            break;
        }
    }

    bool ok = false;
    if (have_config) {
        ok = output_apply_config(output, &merged);
        config_free_wallpaper(&merged);
    } else {
        log_info("Reconnect: no matching config for output %s, using built-in default",
                 name[0] ? name : "unknown");
        struct wallpaper_config def;
        init_wallpaper_config_defaults(&def);
        ok = output_apply_config(output, &def);
    }

    vibe_value_free(root);
    vibe_parser_free(parser);
    return ok;
}

/* Validate every recognized block before config_load_internal() mutates any
 * live state. Parsing may allocate directory/channel lists, so each temporary
 * model is released immediately after validation. */
static bool validate_config_tree(VibeValue *root, bool *out_has_config) {
    bool has_config = false;

    VibeValue *mouse_val = vibe_object_get(root->as_object, "mouse_interaction");
    if (mouse_val && mouse_val->type != VIBE_TYPE_BOOLEAN) {
        log_error("Top-level 'mouse_interaction' must be a boolean (true or false)");
        return false;
    }

    VibeValue *raw_val = vibe_object_get(root->as_object, "term_raw_input");
    if (raw_val && raw_val->type != VIBE_TYPE_BOOLEAN) {
        log_error("Top-level 'term_raw_input' must be a boolean (true or false)");
        return false;
    }

    VibeValue *default_obj = vibe_object_get(root->as_object, "default");
    if (default_obj) {
        if (default_obj->type != VIBE_TYPE_OBJECT) {
            log_error("Default configuration must be an object");
            return false;
        }
        struct wallpaper_config config = {0};
        if (!parse_wallpaper_config(default_obj, &config, "default")) {
            config_free_wallpaper(&config);
            log_error("Default configuration validation failed");
            return false;
        }
        config_free_wallpaper(&config);
        has_config = true;
    }

    VibeValue *outputs_obj = vibe_object_get(root->as_object, "output");
    if (!outputs_obj) outputs_obj = vibe_object_get(root->as_object, "outputs");
    if (outputs_obj) {
        if (outputs_obj->type != VIBE_TYPE_OBJECT) {
            log_error("Top-level 'output'/'outputs' must be an object");
            return false;
        }
        for (size_t i = 0; i < outputs_obj->as_object->count; i++) {
            const char *name = outputs_obj->as_object->entries[i].key;
            VibeValue *obj = outputs_obj->as_object->entries[i].value;
            if (obj->type != VIBE_TYPE_OBJECT) {
                log_error("Configuration for output '%s' must be an object", name);
                return false;
            }
            char context[128];
            snprintf(context, sizeof(context), "output.%s", name);
            struct wallpaper_config config = {0};
            if (!parse_wallpaper_config(obj, &config, context)) {
                config_free_wallpaper(&config);
                log_error("Configuration validation failed for output '%s'", name);
                return false;
            }
            config_free_wallpaper(&config);
            has_config = true;
        }
    }

    if (!has_config) {
        log_error("Config contains no default or output configuration blocks");
        return false;
    }
    *out_has_config = true;
    return true;
}

static bool config_load_internal(struct neowall_state *state, const char *config_path,
                                 bool allow_builtin_fallback) {
    if (!state || !config_path) {
        log_error("Invalid parameters for config_load");
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    log_info("========================================");
    log_info("Loading configuration from: %s", config_path);
    log_info("========================================");

    /* Check if file exists, creating a starter config only during initial load. */
    struct stat st;
    if (stat(config_path, &st) == -1) {
        if (!allow_builtin_fallback) {
            log_error("Reload could not stat configuration '%s': %s; live configuration unchanged",
                      config_path, strerror(errno));
            return false;
        }
        log_info("Configuration file not found, creating default: %s", config_path);
        if (!config_create_default(config_path)) {
            log_error("Failed to create default configuration, using built-in defaults");
            return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
        }
        /* Re-stat to get the new file */
        if (stat(config_path, &st) == -1) {
            log_error("Failed to stat newly created config file, using built-in defaults");
            return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
        }
    }

    /* Validate file is a regular file before opening (security check) */
    if (!S_ISREG(st.st_mode)) {
        log_error("Config path is not a regular file (mode=0%o), using built-in defaults",
                 st.st_mode);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* Sanity check file size (config should be reasonable, < 1MB) */
    if (st.st_size > 1024 * 1024) {
        log_error("Config file too large (%ld bytes), using built-in defaults",
                 (long)st.st_size);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* Read file content with error handling */
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        /* File might have been deleted between stat and open (race condition) */
        if (errno == ENOENT) {
            log_error("Config file disappeared between stat and open (race), using built-in defaults");
        } else {
            log_error("Failed to open config file: %s, using built-in defaults", strerror(errno));
        }
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* Use file size from stat (already validated) */
    long file_size = st.st_size;

    /* Allocate buffer for file content */
    char *content = malloc(file_size + 1);
    if (!content) {
        log_error("Failed to allocate %ld bytes for config, using built-in defaults",
                 file_size + 1);
        fclose(fp);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* Read entire file in one operation */
    size_t bytes_read = fread(content, 1, file_size, fp);
    content[bytes_read] = '\0';
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        log_error("Failed to read config file (expected %ld, got %zu), using built-in defaults",
                 file_size, bytes_read);
        free(content);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* Parse VIBE */
    VibeParser *parser = vibe_parser_new();
    if (!parser) {
        log_error("Failed to create VIBE parser, using built-in defaults");
        free(content);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    VibeValue *root = vibe_parse_string(parser, content);
    free(content);

    if (!root) {
        VibeError error = vibe_get_last_error(parser);
        if (error.has_error) {
            log_error("========================================");
            log_error("CONFIG PARSE ERROR");
            log_error("========================================");
            log_error("File: %s", config_path);
            log_error("Line: %d, Column: %d", error.line, error.column);
            log_error("Error: %s", error.message);
            log_error("========================================");
            log_error("Using built-in default configuration");
            log_error("Fix the config file to resolve this issue");
            log_error("========================================");
        } else {
            log_error("Failed to parse VIBE config, using built-in defaults");
        }
        vibe_parser_free(parser);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    if (root->type != VIBE_TYPE_OBJECT) {
        log_error("Config root must be an object%s",
                  allow_builtin_fallback ? ", using built-in defaults" : "");
        vibe_value_free(root);
        vibe_parser_free(parser);
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* This is the transaction boundary for syntax/type/semantic validation:
     * do not store a global or call output_apply_config() until the complete
     * parsed model has passed. In particular, a malformed later output block
     * can no longer leave earlier outputs running a new configuration. */
    bool validated_has_config = false;
    if (!validate_config_tree(root, &validated_has_config)) {
        vibe_value_free(root);
        vibe_parser_free(parser);
        log_error("Configuration rejected; live configuration is unchanged");
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }

    /* Track if we successfully applied any configuration */
    bool config_applied = false;
    bool application_failed = false;

    /* Parse top-level (global) options. These live at the root of the config,
     * not inside a default/output block, because they configure the daemon
     * process as a whole — not a single wallpaper surface. */
    VibeValue *mouse_val = vibe_object_get(root->as_object, "mouse_interaction");
    if (mouse_val) {
        if (mouse_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("Top-level 'mouse_interaction' must be a boolean (true or false), "
                      "got type: %d — ignoring, keeping default (enabled)",
                      mouse_val->type);
        } else {
            atomic_store_explicit(&state->mouse_interaction,
                                  mouse_val->as_boolean,
                                  memory_order_release);
            log_info("Mouse interaction: %s",
                     mouse_val->as_boolean ? "enabled (default)"
                                           : "disabled (no pointer events, iMouse stays at center)");
        }
    }

    /* term_raw_input: when true, a focused terminal wallpaper receives every
     * key verbatim including the signal/EOF/suspend control keys. Default
     * false, which drops Ctrl-C/D/\\/Z so a stray keypress can't tear down the
     * app running as your wallpaper. Daemon-global, top-level only. */
    VibeValue *raw_val = vibe_object_get(root->as_object, "term_raw_input");
    if (raw_val) {
        if (raw_val->type != VIBE_TYPE_BOOLEAN) {
            log_error("Top-level 'term_raw_input' must be a boolean (true or false), "
                      "got type: %d — ignoring, keeping default (filtered)",
                      raw_val->type);
        } else {
            atomic_store_explicit(&state->term_raw_input,
                                  raw_val->as_boolean,
                                  memory_order_release);
            log_info("Terminal raw input: %s",
                     raw_val->as_boolean
                         ? "enabled (Ctrl-C/D/\\/Z reach the wallpaper app)"
                         : "disabled (default — signal/EOF keys dropped)");
        }
    }

    /* Parse default configuration */
    VibeValue *default_obj = vibe_object_get(root->as_object, "default");
    struct wallpaper_config default_config = {0};
    bool has_valid_default = false;

    if (default_obj && default_obj->type == VIBE_TYPE_OBJECT) {
        if (parse_wallpaper_config(default_obj, &default_config, "default")) {
            has_valid_default = true;

            const char *type_str = (default_config.type == WALLPAPER_SHADER) ? "shader" : "image";
            const char *path_str = (default_config.type == WALLPAPER_SHADER) ?
                                   default_config.shader_path : default_config.path;

            log_info("Valid default configuration: type=%s, path=%s, mode=%s",
                     type_str, path_str, wallpaper_mode_to_string(default_config.mode));

            /* Apply default config to all outputs. Traversal needs the read
             * lock like the output-specific branch below. */
            pthread_rwlock_rdlock(&state->output_list_lock);
            struct output_state *output = state->outputs;
            while (output) {
                /* Copy default config */
                struct wallpaper_config config_copy;
                memcpy(&config_copy, &default_config, sizeof(struct wallpaper_config));

                /* Duplicate cycle paths if present */
                if (default_config.cycle && default_config.cycle_count > 0) {
                    config_copy.cycle_paths = calloc(default_config.cycle_count, sizeof(char *));
                    if (config_copy.cycle_paths) {
                        for (size_t i = 0; i < default_config.cycle_count; i++) {
                            config_copy.cycle_paths[i] = strdup(default_config.cycle_paths[i]);
                        }
                    }
                }

                /* Duplicate channel paths if present */
                if (default_config.channel_count > 0) {
                    config_copy.channel_paths = calloc(default_config.channel_count, sizeof(char *));
                    if (config_copy.channel_paths) {
                        for (size_t i = 0; i < default_config.channel_count; i++) {
                            config_copy.channel_paths[i] = strdup(default_config.channel_paths[i]);
                        }
                    }
                }

                /* output_apply_config() deep-copies the path arrays into the
                 * output's own config rather than taking ownership, so the
                 * copies we just made here must be freed afterwards or they
                 * leak once per output per config load. */
                if (!output_apply_config(output, &config_copy)) {
                    log_error("Failed to apply default config to output '%s'",
                              output->connector_name[0] ? output->connector_name : output->model);
                    application_failed = true;
                }
                config_free_wallpaper(&config_copy);
                output = output->next;
            }
            pthread_rwlock_unlock(&state->output_list_lock);

            config_applied = true;
        } else {
            log_error("Default configuration validation failed");
        }
    } else {
        log_debug("No default configuration block found");
    }

    /* Parse output-specific configurations - accept both "output" and "outputs" */
    VibeValue *outputs_obj = vibe_object_get(root->as_object, "output");
    if (!outputs_obj) {
        outputs_obj = vibe_object_get(root->as_object, "outputs");
    }

    if (outputs_obj && outputs_obj->type == VIBE_TYPE_OBJECT) {
        /* Iterate through all output names */
        for (size_t i = 0; i < outputs_obj->as_object->count; i++) {
            const char *output_name = outputs_obj->as_object->entries[i].key;
            VibeValue *output_config_obj = outputs_obj->as_object->entries[i].value;

            if (output_config_obj->type != VIBE_TYPE_OBJECT) {
                log_error("Configuration for output '%s' must be an object", output_name);
                continue;
            }

            char context[128];
            snprintf(context, sizeof(context), "output.%s", output_name);

            struct wallpaper_config output_config;
            if (parse_wallpaper_config(output_config_obj, &output_config, context)) {
                const char *type_str = (output_config.type == WALLPAPER_SHADER) ? "shader" : "image";
                const char *path_str = (output_config.type == WALLPAPER_SHADER) ?
                                       output_config.shader_path : output_config.path;

                log_info("Valid configuration for output '%s': type=%s, path=%s, mode=%s",
                         output_name, type_str, path_str,
                         wallpaper_mode_to_string(output_config.mode));

                /* Find matching output and apply config - use read lock for safe traversal */
                pthread_rwlock_rdlock(&state->output_list_lock);
                struct output_state *target = state->outputs;
                bool found = false;
                while (target) {
                    /* Try connector name first (e.g., HDMI-A-2, DP-1), then fall back to model name */
                    bool matches = false;
                    if (target->connector_name[0] != '\0' && strcmp(target->connector_name, output_name) == 0) {
                        matches = true;
                        log_debug("Matched output by connector name: %s", output_name);
                    } else if (strcmp(target->model, output_name) == 0) {
                        matches = true;
                        log_debug("Matched output by model name: %s", output_name);
                    }

                    if (matches) {
                        bool apply_result = output_apply_config(target, &output_config);
                        if (!apply_result) {
                            log_error("Failed to apply config to output '%s'", output_name);
                            application_failed = true;
                        } else {
                            log_info("Applied configuration to output '%s'", output_name);
                            config_applied = true;
                        }
                        found = true;
                        break;
                    }
                    target = target->next;
                }
                /* Release the traversal lock before touching output_config or
                 * looping to the next config block. Previously this rdlock was
                 * never released, recursively re-locking on each iteration and
                 * permanently starving any writer (output hotplug/removal). */
                pthread_rwlock_unlock(&state->output_list_lock);

                if (!found) {
                    log_debug("Output '%s' not connected yet, config saved for when it appears",
                             output_name);
                }
                /* output_apply_config() deep-copies into the target's own
                 * config, so the arrays parse_wallpaper_config() allocated in
                 * output_config are ours to free on every path (matched or
                 * not) — previously they leaked whenever a match was found. */
                config_free_wallpaper(&output_config);
            } else {
                log_error("Configuration validation failed for output '%s'", output_name);
            }
        }
    }

    /* Clean up */
    if (has_valid_default) {
        config_free_wallpaper(&default_config);
    }
    vibe_value_free(root);
    vibe_parser_free(parser);

    /* Backends may need to react to globals parsed above (e.g. release
     * wl_pointer if mouse_interaction=false but the seat event already
     * fired before we got here). Optional op, no-op for backends that
     * don't implement it. */
    if (state->compositor_backend && state->compositor_backend->ops &&
        state->compositor_backend->ops->apply_input_config) {
        state->compositor_backend->ops->apply_input_config(
            state->compositor_backend->data, state);
    }

    if (application_failed) {
        log_error("Configuration was valid, but one or more resources failed to apply");
        log_error("No built-in fallback will be applied after an application failure");
        return false;
    }

    if (config_applied || (validated_has_config && !allow_builtin_fallback)) {
        log_info("========================================");
        log_info("[OK] Configuration loaded successfully from %s", config_path);

        /* Print summary of what was configured - use read lock for safe traversal */
        pthread_rwlock_rdlock(&state->output_list_lock);
        int output_count = 0;
        int shader_count = 0;
        int image_count = 0;
        struct output_state *summary_output = state->outputs;
        while (summary_output) {
            output_count++;
            /* Safety check: ensure config pointer is valid */
            if (!summary_output->config) {
                log_error("Output %s has NULL config pointer, skipping summary",
                         summary_output->model[0] ? summary_output->model : "unknown");
                summary_output = summary_output->next;
                continue;
            }
            if (summary_output->config->type == WALLPAPER_SHADER) {
                shader_count++;
                log_info("  Output %s: SHADER mode - %s (speed=%.1fx)",
                         summary_output->model[0] ? summary_output->model : "unknown",
                         summary_output->config->shader_path,
                         summary_output->config->shader_speed);
            } else {
                image_count++;
                log_info("  Output %s: IMAGE mode - %s (mode=%s)",
                         summary_output->model[0] ? summary_output->model : "unknown",
                         summary_output->config->path,
                         wallpaper_mode_to_string(summary_output->config->mode));
            }
            if (summary_output->config->cycle && summary_output->config->cycle_count > 1) {
                log_info("    -> Cycling through %zu items, duration=%.0fs",
                         summary_output->config->cycle_count, summary_output->config->duration);
            }
            summary_output = summary_output->next;
        }
        pthread_rwlock_unlock(&state->output_list_lock);
        log_info("Total: %d output(s) configured (%d shader, %d image)",
                 output_count, shader_count, image_count);
        log_info("========================================");
        return true;
    } else {
        log_error("========================================");
        log_error("[ERROR] No valid configuration found in file");
        log_error("========================================");
        log_error("The config file was parsed but contains no valid settings");
        log_error("Using built-in default configuration");
        log_error("========================================");
        return allow_builtin_fallback ? apply_builtin_default_config(state) : false;
    }
}

bool config_load(struct neowall_state *state, const char *config_path) {
    return config_load_internal(state, config_path, true);
}

bool config_reload(struct neowall_state *state, const char *config_path) {
    return config_load_internal(state, config_path, false);
}
