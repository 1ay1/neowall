#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <dirent.h>
#include "config.h"
#include "vibe.h"
#include "neowall.h"
#include "../image/image.h"    /* For image_free() */
#include "config_access.h"
#include "compositor.h"
#include "shader.h"

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

    /* Try /etc */
    snprintf(path, sizeof(path), "/etc/neowall/config.vibe");
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

enum wallpaper_mode wallpaper_mode_from_string(const char *str) {
    if (!str) return MODE_FILL;  /* Safe default */

    for (size_t i = 0; i < mode_mapping_count; i++) {
        if (strcasecmp(str, mode_mappings[i].name) == 0) {
            return mode_mappings[i].mode;
        }
    }

    log_error("Invalid wallpaper mode '%s', using 'fill' as default", str);
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

enum transition_type transition_type_from_string(const char *str) {
    if (!str) return TRANSITION_FADE;  /* Safe default */

    for (size_t i = 0; i < transition_mapping_count; i++) {
        if (strcasecmp(str, transition_mappings[i].name) == 0) {
            log_debug("Matched transition '%s' to type %d", str, transition_mappings[i].type);
            return transition_mappings[i].type;
        }
        if (transition_mappings[i].alias &&
            strcasecmp(str, transition_mappings[i].alias) == 0) {
            log_debug("Matched transition '%s' (via alias) to type %d", str, transition_mappings[i].type);
            return transition_mappings[i].type;
        }
    }

    log_error("Invalid transition type '%s', using 'fade' as default", str);
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

/* ============================================================================
 * Directory Loading Functions
 * ============================================================================ */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
char **load_shaders_from_directory(const char *dir_path, size_t *count) {
    if (!dir_path || !count) return NULL;

    *count = 0;

    /* Expand ~ to home directory */
    char expanded_path[MAX_PATH_LENGTH];
    if (dir_path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~ without HOME environment variable");
            return NULL;
        }
        snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, dir_path + 1);
    } else {
        strncpy(expanded_path, dir_path, sizeof(expanded_path) - 1);
        expanded_path[sizeof(expanded_path) - 1] = '\0';
    }

    /* Strip trailing slash to avoid double slashes in path construction */
    size_t path_len = strlen(expanded_path);
    if (path_len > 1 && expanded_path[path_len - 1] == '/') {
        expanded_path[path_len - 1] = '\0';
    }

    DIR *dir = opendir(expanded_path);
    if (!dir) {
        return NULL;  /* Not a directory */
    }

    /* First pass: count shader files */
    struct dirent *entry;
    size_t shader_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            if (is_shader_file(entry->d_name)) {
                shader_count++;
            }
        }
    }

    if (shader_count == 0) {
        closedir(dir);
        return NULL;
    }

    /* Allocate array for paths */
    char **paths = calloc(shader_count, sizeof(char *));
    if (!paths) {
        closedir(dir);
        return NULL;
    }

    /* Second pass: collect shader file paths */
    rewinddir(dir);
    size_t index = 0;
    while ((entry = readdir(dir)) != NULL && index < shader_count) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            if (is_shader_file(entry->d_name)) {
                char full_path[MAX_PATH_LENGTH];
                size_t expanded_len = strlen(expanded_path);
                size_t name_len = strlen(entry->d_name);

                /* Check if concatenated path would fit */
                if (expanded_len + name_len + 2 >= MAX_PATH_LENGTH) {
                    log_error("Path too long: %s/%s", expanded_path, entry->d_name);
                    continue;
                }

                (void)snprintf(full_path, sizeof(full_path), "%s/%s",
                        expanded_path, entry->d_name);
                paths[index++] = strdup(full_path);
            }
        }
    }
    closedir(dir);

    /* Sort alphabetically for deterministic order */
    qsort(paths, shader_count, sizeof(char *), compare_strings);

    *count = shader_count;
    return paths;
}
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
char **load_images_from_directory(const char *dir_path, size_t *count) {
    if (!dir_path || !count) return NULL;

    *count = 0;

    /* Expand ~ to home directory */
    char expanded_path[MAX_PATH_LENGTH];
    if (dir_path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~ without HOME environment variable");
            return NULL;
        }
        snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, dir_path + 1);
    } else {
        strncpy(expanded_path, dir_path, sizeof(expanded_path) - 1);
        expanded_path[sizeof(expanded_path) - 1] = '\0';
    }

    DIR *dir = opendir(expanded_path);
    if (!dir) {
        return NULL;  /* Not a directory */
    }

    /* First pass: count image files */
    struct dirent *entry;
    size_t image_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            if (is_image_file(entry->d_name)) {
                image_count++;
            }
        }
    }

    if (image_count == 0) {
        closedir(dir);
        return NULL;
    }

    /* Allocate array for paths */
    char **paths = calloc(image_count, sizeof(char *));
    if (!paths) {
        closedir(dir);
        return NULL;
    }

    /* Second pass: collect image file paths */
    rewinddir(dir);
    size_t index = 0;
    while ((entry = readdir(dir)) != NULL && index < image_count) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            if (is_image_file(entry->d_name)) {
                char full_path[MAX_PATH_LENGTH];
                size_t expanded_len = strlen(expanded_path);
                size_t name_len = strlen(entry->d_name);

                /* Check if concatenated path would fit */
                if (expanded_len + name_len + 2 >= MAX_PATH_LENGTH) {
                    log_error("Path too long: %s/%s", expanded_path, entry->d_name);
                    continue;
                }

                (void)snprintf(full_path, sizeof(full_path), "%s/%s",
                        expanded_path, entry->d_name);
                paths[index++] = strdup(full_path);
            }
        }
    }
    closedir(dir);

    /* Sort alphabetically for deterministic order */
    qsort(paths, image_count, sizeof(char *), compare_strings);

    *count = image_count;
    return paths;
}
#pragma GCC diagnostic pop

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
    config->vsync = false;  /* Default: vsync off, use custom FPS with tearing control */
    config->show_fps = false;  /* Default: no FPS watermark */
    config->cycle = false;
    config->cycle_paths = NULL;
    config->cycle_count = 0;
    config->current_cycle_index = 0;
    config->channel_paths = NULL;
    config->channel_count = 0;
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

    /* Check for 'path' and 'shader' - these are MUTUALLY EXCLUSIVE */
    VibeValue *path_val = vibe_object_get(obj->as_object, "path");
    VibeValue *shader_val = vibe_object_get(obj->as_object, "shader");

    bool has_path = (path_val != NULL && path_val->type == VIBE_TYPE_STRING);
    bool has_shader = (shader_val != NULL && shader_val->type == VIBE_TYPE_STRING);

    /* RULE: path and shader are mutually exclusive */
    if (has_path && has_shader) {
        log_error("[%s] INVALID CONFIG: Both 'path' and 'shader' specified. "
                 "These are mutually exclusive. Use EITHER 'path' for images "
                 "OR 'shader' for GLSL shaders, not both.", context_name);
        return false;
    }

    /* RULE: At least one must be specified */
    if (!has_path && !has_shader) {
        log_error("[%s] INVALID CONFIG: Neither 'path' nor 'shader' specified. "
                 "You must specify exactly one.", context_name);
        return false;
    }

    /* ========================================================================
     * IMAGE MODE: path is specified, shader is not
     * ======================================================================== */
    if (has_path) {
        const char *path_str = path_val->as_string;

        ValidationResult path_validation = validate_path(path_str);
        if (!path_validation.valid) {
            log_error("[%s] Invalid path: %s", context_name,
                     path_validation.error_message);
            return false;
        }

        config->type = WALLPAPER_IMAGE;

        /* Check if path is a directory (ends with /) or actually is a directory */
        size_t path_len = strlen(path_str);
        bool is_dir_syntax = (path_len > 0 && path_str[path_len - 1] == '/');

        /* Try to load as directory */
        size_t image_count = 0;
        char **image_paths = load_images_from_directory(path_str, &image_count);

        if (image_paths && image_count > 0) {
            /* It's a directory with images - enable cycling */
            config->cycle = true;
            config->cycle_count = image_count;
            config->cycle_paths = image_paths;
            config->current_cycle_index = 0;  /* Will be restored per-output in output_apply_config */

            /* Use first image as initial path (will be updated per-output) */
            strncpy(config->path, image_paths[0], sizeof(config->path) - 1);
            config->path[sizeof(config->path) - 1] = '\0';

            log_info("[%s] IMAGE MODE: Loaded %zu images from directory for cycling",
                    context_name, image_count);
        } else if (is_dir_syntax) {
            /* User specified directory syntax but no images found */
            log_error("[%s] Path ends with '/' indicating directory, "
                     "but no images found in '%s'", context_name, path_str);
            return false;
        } else {
            /* Single image file */
            strncpy(config->path, path_str, sizeof(config->path) - 1);
            config->path[sizeof(config->path) - 1] = '\0';

            log_info("[%s] IMAGE MODE: Single image '%s'", context_name, path_str);
        }
    }

    /* ========================================================================
     * SHADER MODE: shader is specified, path is not
     * ======================================================================== */
    if (has_shader) {
        const char *shader_str = shader_val->as_string;

        ValidationResult shader_validation = validate_path(shader_str);
        if (!shader_validation.valid) {
            log_error("[%s] Invalid shader path: %s", context_name,
                     shader_validation.error_message);
            return false;
        }

        config->type = WALLPAPER_SHADER;

        /* Check if shader is a directory (ends with /) or actually is a directory */
        size_t shader_len = strlen(shader_str);
        bool is_dir_syntax = (shader_len > 0 && shader_str[shader_len - 1] == '/');

        /* Try to load as directory */
        size_t shader_count = 0;
        char **shader_paths = load_shaders_from_directory(shader_str, &shader_count);

        if (shader_paths && shader_count > 0) {
            /* It's a directory with shaders - enable cycling */
            config->cycle = true;
            config->cycle_count = shader_count;
            config->cycle_paths = shader_paths;
            config->current_cycle_index = 0;  /* Will be restored per-output in output_apply_config */

            /* Use first shader as initial shader_path (will be updated per-output) */
            strncpy(config->shader_path, shader_paths[0], sizeof(config->shader_path) - 1);
            config->shader_path[sizeof(config->shader_path) - 1] = '\0';

            log_info("[%s] SHADER MODE: Loaded %zu shaders from directory for cycling",
                    context_name, shader_count);
        } else if (is_dir_syntax) {
            /* User specified directory syntax but no shaders found */
            log_error("[%s] Shader path ends with '/' indicating directory, "
                     "but no shaders found in '%s'", context_name, shader_str);
            return false;
        } else {
            /* Single shader file */
            strncpy(config->shader_path, shader_str, sizeof(config->shader_path) - 1);
            config->shader_path[sizeof(config->shader_path) - 1] = '\0';

            log_info("[%s] SHADER MODE: Single shader '%s'", context_name, shader_str);
        }
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

        config->mode = wallpaper_mode_from_string(mode_val->as_string);
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

        config->transition = transition_type_from_string(transition_val->as_string);

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
        int fps = 0;

        if (shader_fps_val->type == VIBE_TYPE_INTEGER) {
            fps = (int)shader_fps_val->as_integer;
        } else if (shader_fps_val->type == VIBE_TYPE_FLOAT) {
            fps = (int)shader_fps_val->as_float;
        } else {
            log_error("[%s] 'shader_fps' must be a number", context_name);
            return false;
        }

        if (fps < 1 || fps > 240) {
            log_error("[%s] Invalid shader_fps: %d (must be between 1 and 240)",
                     context_name, fps);
            return false;
        }

        config->shader_fps = fps;
        log_info("[%s] Shader FPS set to: %d", context_name, fps);

        if (config->type != WALLPAPER_SHADER) {
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

        if (config->type != WALLPAPER_SHADER) {
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

    /* Parse channels (only relevant for shader mode) */
    VibeValue *channels_val = vibe_object_get(obj->as_object, "channels");
    if (channels_val) {
        if (channels_val->type != VIBE_TYPE_ARRAY) {
            log_error("[%s] 'channels' must be an array", context_name);
            return false;
        }

        size_t count = channels_val->as_array->count;
        if (count > 0) {
            if (config->type != WALLPAPER_SHADER) {
                log_error("[%s] INVALID CONFIG: 'channels' specified in IMAGE mode. "
                         "Channels (iChannel textures) only apply to GLSL shaders. This setting is invalid for images.",
                         context_name);
                return false;
            }

            config->channel_count = count;
            config->channel_paths = calloc(count, sizeof(char *));

            if (!config->channel_paths) {
                log_error("[%s] Failed to allocate memory for channels", context_name);
                return false;
            }

            for (size_t i = 0; i < count; i++) {
                VibeValue *elem = channels_val->as_array->values[i];
                if (!elem || elem->type != VIBE_TYPE_STRING) {
                    log_error("[%s] Channel[%zu] must be a string", context_name, i);
                    /* Free already allocated */
                    for (size_t j = 0; j < i; j++) {
                        free(config->channel_paths[j]);
                    }
                    free(config->channel_paths);
                    config->channel_paths = NULL;
                    config->channel_count = 0;
                    return false;
                }

                config->channel_paths[i] = strdup(elem->as_string);
                log_debug("[%s] iChannel%zu: %s", context_name, i, elem->as_string);
            }

            log_info("[%s] Loaded %zu iChannel texture assignments", context_name, count);
        }
    }

    /* Warn about unknown keys */
    const char *known_keys[] = {
        "path", "shader", "mode", "duration", "transition",
        "transition_duration", "shader_speed", "channels", "shader_fps", "vsync", "show_fps"
    };
    size_t known_key_count = sizeof(known_keys) / sizeof(known_keys[0]);

    for (size_t i = 0; i < obj->as_object->count; i++) {
        const char *key = obj->as_object->entries[i].key;
        bool is_known = false;

        for (size_t j = 0; j < known_key_count; j++) {
            if (strcmp(key, known_keys[j]) == 0) {
                is_known = true;
                break;
            }
        }

        if (!is_known) {
            log_info("[%s] Unknown configuration key '%s' (will be ignored)",
                    context_name, key);
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static bool config_create_default(const char *config_path) {
    if (!config_path) {
        return false;
    }

    /* Get directory path */
    char dir_path[MAX_PATH_LENGTH];
    strncpy(dir_path, config_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    /* Find last slash to get directory */
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(dir_path, &st) == -1) {
        /* Create directory recursively */
        char tmp[MAX_PATH_LENGTH];
        char *p = NULL;
        snprintf(tmp, sizeof(tmp), "%s", dir_path);

        for (p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
                    log_error("Failed to create directory %s: %s", tmp, strerror(errno));
                    return false;
                }
                *p = '/';
            }
        }

        if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
            log_error("Failed to create directory %s: %s", tmp, strerror(errno));
            return false;
        }
    }

    /* Get the installation path or use built-in default */
    const char *default_wallpaper_path = NULL;

    /* Copy default wallpaper to user's local directory if it doesn't exist */
    const char *home = getenv("HOME");
    if (home) {
        char user_wallpaper_dir[MAX_PATH_LENGTH];
        char user_wallpaper_path[MAX_PATH_LENGTH];
        int ret1 = snprintf(user_wallpaper_dir, sizeof(user_wallpaper_dir),
                           "%s/.local/share/neowall", home);
        if (ret1 < 0 || (size_t)ret1 >= sizeof(user_wallpaper_dir)) {
            log_error("Path too long for user wallpaper directory");
            default_wallpaper_path = "~/Pictures/wallpaper.png";
        } else {
            int ret2 = snprintf(user_wallpaper_path, sizeof(user_wallpaper_path),
                               "%s/default.png", user_wallpaper_dir);
            if (ret2 < 0 || (size_t)ret2 >= sizeof(user_wallpaper_path)) {
                log_error("Path too long for user wallpaper path");
                default_wallpaper_path = "~/Pictures/wallpaper.png";
            } else {
                /* Check if user already has the default wallpaper */
                if (access(user_wallpaper_path, F_OK) != 0) {
                    /* Try to find and copy the default wallpaper from installation */
                    const char *source_paths[] = {
                        "/usr/share/neowall/default.png",
                        "/usr/local/share/neowall/default.png",
                        NULL
                    };

                    for (int i = 0; source_paths[i] != NULL; i++) {
                        if (access(source_paths[i], R_OK) == 0) {
                            /* Create user wallpaper directory recursively */
                            char tmp[MAX_PATH_LENGTH];
                            snprintf(tmp, sizeof(tmp), "%s/.local", home);
                            mkdir(tmp, 0755);
                            snprintf(tmp, sizeof(tmp), "%s/.local/share", home);
                            mkdir(tmp, 0755);
                            mkdir(user_wallpaper_dir, 0755);

                            /* Copy the file */
                            FILE *src = fopen(source_paths[i], "rb");
                            if (src) {
                                FILE *dst = fopen(user_wallpaper_path, "wb");
                                if (dst) {
                                    char buffer[4096];
                                    size_t bytes;
                                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                                        if (fwrite(buffer, 1, bytes, dst) != bytes) {
                                            log_error("Failed to write to %s", user_wallpaper_path);
                                            break;
                                        }
                                    }
                                    fclose(dst);
                                    log_info("Copied default wallpaper to %s", user_wallpaper_path);
                                }
                                fclose(src);
                            }
                            break;
                        }
                    }
                }

                /* Use the user's local copy if it exists */
                if (access(user_wallpaper_path, F_OK) == 0) {
                    default_wallpaper_path = "~/.local/share/neowall/default.png";
                } else {
                    default_wallpaper_path = "~/Pictures/wallpaper.png";
                }
            }
        }
    } else {
        default_wallpaper_path = "~/Pictures/wallpaper.png";
    }

    /* Try to copy config from installation as the main config */
    const char *example_config_paths[] = {
        "/usr/share/neowall/config.vibe",
        "/usr/local/share/neowall/config.vibe",
        NULL
    };

    bool copied_config = false;
    for (int i = 0; example_config_paths[i] != NULL; i++) {
        if (access(example_config_paths[i], R_OK) == 0) {
            /* Copy example config as the main config file */
            FILE *src = fopen(example_config_paths[i], "rb");
            if (src) {
                FILE *dst = fopen(config_path, "wb");
                if (dst) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                        if (fwrite(buffer, 1, bytes, dst) != bytes) {
                            log_error("Failed to write to %s", config_path);
                            break;
                        }
                    }
                    fclose(dst);
                    log_info("Created configuration file from example: %s", config_path);
                    copied_config = true;
                }
                fclose(src);
            }
            break;
        }
    }

    /* Copy detailed example config (neowall.vibe) if available */
    if (home) {
        const char *detailed_config_paths[] = {
            "/usr/share/neowall/neowall.vibe",
            "/usr/local/share/neowall/neowall.vibe",
            NULL
        };

        char detailed_config_path[MAX_PATH_LENGTH];
        snprintf(detailed_config_path, sizeof(detailed_config_path),
                "%s/.config/neowall/neowall.vibe", home);

        for (int i = 0; detailed_config_paths[i] != NULL; i++) {
            if (access(detailed_config_paths[i], R_OK) == 0) {
                FILE *src = fopen(detailed_config_paths[i], "rb");
                if (src) {
                    FILE *dst = fopen(detailed_config_path, "wb");
                    if (dst) {
                        char buffer[4096];
                        size_t bytes;
                        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                            fwrite(buffer, 1, bytes, dst);
                        }
                        fclose(dst);
                        log_info("Copied detailed example config to %s", detailed_config_path);
                    }
                    fclose(src);
                }
                break;
            }
        }
    }

    /* Try to copy example shaders from installation if available */
    if (home) {
        const char *shader_install_paths[] = {
            "/usr/share/neowall/shaders",
            "/usr/local/share/neowall/shaders",
            NULL
        };

        bool copied_shaders = false;
        for (int i = 0; shader_install_paths[i] != NULL; i++) {
            if (access(shader_install_paths[i], R_OK) == 0) {
                /* Create user shader directory */
                char user_shader_dir[MAX_PATH_LENGTH];
                snprintf(user_shader_dir, sizeof(user_shader_dir),
                        "%s/.config/neowall/shaders", home);

                /* Create directory structure */
                char tmp[MAX_PATH_LENGTH];
                snprintf(tmp, sizeof(tmp), "%s/.config", home);
                mkdir(tmp, 0755);
                snprintf(tmp, sizeof(tmp), "%s/.config/neowall", home);
                mkdir(tmp, 0755);
                mkdir(user_shader_dir, 0755);

                /* Copy all shader files */
                DIR *dir = opendir(shader_install_paths[i]);
                if (dir) {
                    struct dirent *entry;
                    int shader_count = 0;
                    while ((entry = readdir(dir)) != NULL) {
                        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
                            /* Check if it's a .glsl or README file */
                            size_t len = strlen(entry->d_name);
                            if ((len > 5 && strcmp(entry->d_name + len - 5, ".glsl") == 0) ||
                                strcmp(entry->d_name, "README.md") == 0) {

                                /* Check path lengths */
                                if (strlen(shader_install_paths[i]) + len + 2 >= MAX_PATH_LENGTH ||
                                    strlen(user_shader_dir) + len + 2 >= MAX_PATH_LENGTH) {
                                    log_error("Shader path too long: %s", entry->d_name);
                                    continue;
                                }

                                char src_path[MAX_PATH_LENGTH];
                                char dst_path[MAX_PATH_LENGTH];
                                (void)snprintf(src_path, sizeof(src_path), "%s/%s",
                                        shader_install_paths[i], entry->d_name);
                                (void)snprintf(dst_path, sizeof(dst_path), "%s/%s",
                                        user_shader_dir, entry->d_name);

                                FILE *src = fopen(src_path, "rb");
                                if (src) {
                                    FILE *dst = fopen(dst_path, "wb");
                                    if (dst) {
                                        char buffer[4096];
                                        size_t bytes;
                                        while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                                            if (fwrite(buffer, 1, bytes, dst) != bytes) {
                                                log_error("Failed to write shader to %s", dst_path);
                                                break;
                                            }
                                        }
                                        fclose(dst);
                                        shader_count++;
                                    }
                                    fclose(src);
                                }
                            }
                        }
                    }
                    closedir(dir);

                    if (shader_count > 0) {
                        log_info("Copied %d example shader(s) to %s",
                                shader_count, user_shader_dir);
                        copied_shaders = true;
                    }
                }
                break;
            }
        }

        if (copied_shaders) {
            log_info("Example shaders available at ~/.config/neowall/shaders/");
        }
    }

    /* If we couldn't copy the example config, create a minimal fallback */
    if (!copied_config) {
        log_info("Could not find example config, creating minimal fallback");

        const char *fallback_config =
            "# NeoWall Configuration\n"
            "# This is a minimal fallback config\n"
            "#\n"
            "# IMPORTANT: 'path' and 'shader' are MUTUALLY EXCLUSIVE\n"
            "# - Use 'path' for images (PNG, JPEG)\n"
            "# - Use 'shader' for GLSL shaders\n"
            "# - DO NOT use both in the same config block\n"
            "#\n"
            "# Image example:\n"
            "#   default {\n"
            "#     path ~/Pictures/wallpaper.png\n"
            "#     mode fill\n"
            "#   }\n"
            "#\n"
            "# Shader example:\n"
            "#   default {\n"
            "#     shader ~/.config/neowall/shaders/plasma.glsl\n"
            "#     shader_speed 1.0\n"
            "#   }\n"
            "#\n"
            "# Directory cycling (add / at end or specify duration):\n"
            "#   default {\n"
            "#     path ~/Pictures/Wallpapers/\n"
            "#     duration 300\n"
            "#     transition fade\n"
            "#   }\n\n"
            "default {\n"
            "  path %s\n"
            "  mode fill\n"
            "}\n";

        FILE *fp = fopen(config_path, "w");
        if (!fp) {
            log_error("Failed to create default config file: %s", strerror(errno));
            return false;
        }

        fprintf(fp, fallback_config, default_wallpaper_path);
        fclose(fp);

        log_info("Created minimal configuration file: %s", config_path);
    }

    if (copied_config) {
        log_info("Edit %s to customize your wallpaper setup", config_path);
    }

return true;
}
#pragma GCC diagnostic pop

/* ============================================================================
 * Built-in fallback configuration (used when config file fails)
 * ============================================================================ */

static bool apply_builtin_default_config(struct neowall_state *state) {
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
                strncpy(expanded, try_paths[i], sizeof(expanded) - 1);
            }

            /* Check if it's a file */
            struct stat st;
            if (stat(expanded, &st) == 0 && S_ISREG(st.st_mode) && access(expanded, R_OK) == 0) {
                strncpy(default_config.path, expanded, sizeof(default_config.path) - 1);
                default_config.path[sizeof(default_config.path) - 1] = '\0';
                log_info("Using default wallpaper: %s", expanded);
                break;
            }

            /* Check if it's a directory with images */
            if (stat(expanded, &st) == 0 && S_ISDIR(st.st_mode)) {
                size_t count = 0;
                char **images = load_images_from_directory(expanded, &count);
                if (images && count > 0) {
                    strncpy(default_config.path, images[0], sizeof(default_config.path) - 1);
                    default_config.path[sizeof(default_config.path) - 1] = '\0';
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

bool config_load(struct neowall_state *state, const char *config_path) {
    if (!state || !config_path) {
        log_error("Invalid parameters for config_load");
        return apply_builtin_default_config(state);
    }

    log_info("========================================");
    log_info("Loading configuration from: %s", config_path);
    log_info("========================================");

    /* Check if file exists, create default if not */
    struct stat st;
    if (stat(config_path, &st) == -1) {
        log_info("Configuration file not found, creating default: %s", config_path);
        if (!config_create_default(config_path)) {
            log_error("Failed to create default configuration, using built-in defaults");
            return apply_builtin_default_config(state);
        }
        /* Re-stat to get the new file */
        if (stat(config_path, &st) == -1) {
            log_error("Failed to stat newly created config file, using built-in defaults");
            return apply_builtin_default_config(state);
        }
    }

    /* Validate file is a regular file before opening (security check) */
    if (!S_ISREG(st.st_mode)) {
        log_error("Config path is not a regular file (mode=0%o), using built-in defaults",
                 st.st_mode);
        return apply_builtin_default_config(state);
    }

    /* Sanity check file size (config should be reasonable, < 1MB) */
    if (st.st_size > 1024 * 1024) {
        log_error("Config file too large (%ld bytes), using built-in defaults",
                 (long)st.st_size);
        return apply_builtin_default_config(state);
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
        return apply_builtin_default_config(state);
    }

    /* Use file size from stat (already validated) */
    long file_size = st.st_size;

    /* Allocate buffer for file content */
    char *content = malloc(file_size + 1);
    if (!content) {
        log_error("Failed to allocate %ld bytes for config, using built-in defaults",
                 file_size + 1);
        fclose(fp);
        return apply_builtin_default_config(state);
    }

    /* Read entire file in one operation */
    size_t bytes_read = fread(content, 1, file_size, fp);
    content[bytes_read] = '\0';
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        log_error("Failed to read config file (expected %ld, got %zu), using built-in defaults",
                 file_size, bytes_read);
        free(content);
        return apply_builtin_default_config(state);
    }

    /* Parse VIBE */
    VibeParser *parser = vibe_parser_new();
    if (!parser) {
        log_error("Failed to create VIBE parser, using built-in defaults");
        free(content);
        return apply_builtin_default_config(state);
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
        return apply_builtin_default_config(state);
    }

    if (root->type != VIBE_TYPE_OBJECT) {
        log_error("Config root must be an object, using built-in defaults");
        vibe_value_free(root);
        vibe_parser_free(parser);
        return apply_builtin_default_config(state);
    }

    /* Track if we successfully applied any configuration */
    bool config_applied = false;

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

            /* Apply default config to all outputs */
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

                output_apply_config(output, &config_copy);
                output = output->next;
            }

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
                        } else {
                            log_info("Applied configuration to output '%s'", output_name);
                            config_applied = true;
                        }
                        found = true;
                        break;
                    }
                    target = target->next;
                }

                if (!found) {
                    log_debug("Output '%s' not connected yet, config saved for when it appears",
                             output_name);
                    config_free_wallpaper(&output_config);
                }
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

    if (config_applied) {
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
        return apply_builtin_default_config(state);
    }
}
