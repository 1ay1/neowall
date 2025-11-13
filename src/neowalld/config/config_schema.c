/*
 * Configuration Schema Implementation
 * Uses config_keys.h as single source of truth
 */

#include "config_schema.h"
#include "config_keys.h"
#include "neowall.h"
#include "output/output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdatomic.h>

/* ============================================================================
 * Apply Functions (Forward Declarations)
 * ============================================================================ */

static bool apply_cycle_interval(struct neowall_state *state, const char *value);
static bool apply_shader_speed(struct neowall_state *state, const char *value);
static bool apply_output_cycle_interval(struct output_state *output, const char *value);
static bool apply_output_mode(struct output_state *output, const char *value);
static bool apply_output_shader_speed(struct output_state *output, const char *value);
static bool apply_output_transition(struct output_state *output, const char *value);
static bool apply_output_transition_duration(struct output_state *output, const char *value);

/* ============================================================================
 * Configuration Schema Registry
 * ============================================================================ */

/* Helper to create enum values array */
static const char *mode_enum_values[] = {"center", "stretch", "fit", "fill", "tile", NULL};
static const char *transition_enum_values[] = {"none", "fade", "slide-left", "slide-right", "glitch", "pixelate", NULL};

/* Generate the config_keys array using the macro */
#define CONFIG_ENTRY(key_const, sec, nm, typ, scp) \
    { \
        .key = key_const, \
        .section = sec, \
        .name = nm, \
        .type = typ, \
        .scope = scp, \
        .description = get_description_for_##sec##_##nm(), \
        .example = get_example_for_##sec##_##nm(), \
        .default_value = get_default_for_##sec##_##nm(), \
        .constraints = get_constraints_for_##sec##_##nm(), \
        .validate = NULL, \
        .apply = get_apply_for_##sec##_##nm(), \
        .apply_output = get_apply_output_for_##sec##_##nm(), \
    },

/* Metadata functions - description */
static inline const char *get_description_for_default_path(void) { return "Path to wallpaper image or directory (directory must end with /)"; }
static inline const char *get_description_for_default_shader(void) { return "GLSL shader file for animated wallpaper"; }
static inline const char *get_description_for_default_mode(void) { return "Display mode for wallpaper"; }
static inline const char *get_description_for_default_duration(void) { return "Wallpaper cycling interval in seconds (0=disabled)"; }
static inline const char *get_description_for_default_transition(void) { return "Transition effect when changing wallpapers"; }
static inline const char *get_description_for_default_transition_duration(void) { return "Transition duration in milliseconds"; }
static inline const char *get_description_for_default_shader_speed(void) { return "Shader animation speed multiplier"; }
static inline const char *get_description_for_default_shader_fps(void) { return "Target FPS for shader rendering"; }
static inline const char *get_description_for_default_vsync(void) { return "Sync shader rendering to monitor refresh rate"; }
static inline const char *get_description_for_default_show_fps(void) { return "Show FPS counter on screen"; }
static inline const char *get_description_for_default_channels(void) { return "Shader texture channels (iChannel0-3)"; }
static inline const char *get_description_for_output_path(void) { return "Path to wallpaper image or directory for this output"; }
static inline const char *get_description_for_output_shader(void) { return "GLSL shader file for this output"; }
static inline const char *get_description_for_output_mode(void) { return "Display mode for this output"; }
static inline const char *get_description_for_output_duration(void) { return "Cycling interval for this output in seconds"; }
static inline const char *get_description_for_output_transition(void) { return "Transition effect for this output"; }
static inline const char *get_description_for_output_transition_duration(void) { return "Transition duration for this output in milliseconds"; }
static inline const char *get_description_for_output_shader_speed(void) { return "Shader animation speed for this output"; }
static inline const char *get_description_for_output_shader_fps(void) { return "Target FPS for shader rendering on this output"; }
static inline const char *get_description_for_output_vsync(void) { return "Sync shader rendering to monitor refresh rate for this output"; }
static inline const char *get_description_for_output_show_fps(void) { return "Show FPS counter on screen for this output"; }
static inline const char *get_description_for_output_channels(void) { return "Shader texture channels for this output"; }

/* Metadata functions - example */
static inline const char *get_example_for_default_path(void) { return "~/Pictures/wallpapers/"; }
static inline const char *get_example_for_default_shader(void) { return "retro_wave.glsl"; }
static inline const char *get_example_for_default_mode(void) { return "fill"; }
static inline const char *get_example_for_default_duration(void) { return "300"; }
static inline const char *get_example_for_default_transition(void) { return "fade"; }
static inline const char *get_example_for_default_transition_duration(void) { return "500"; }
static inline const char *get_example_for_default_shader_speed(void) { return "1.5"; }
static inline const char *get_example_for_default_shader_fps(void) { return "60"; }
static inline const char *get_example_for_default_vsync(void) { return "true"; }
static inline const char *get_example_for_default_show_fps(void) { return "false"; }
static inline const char *get_example_for_default_channels(void) { return "[\"gray-noise\", \"rgba-noise\"]"; }
static inline const char *get_example_for_output_path(void) { return "~/Pictures/monitor1.jpg"; }
static inline const char *get_example_for_output_shader(void) { return "plasma.glsl"; }
static inline const char *get_example_for_output_mode(void) { return "fit"; }
static inline const char *get_example_for_output_duration(void) { return "600"; }
static inline const char *get_example_for_output_transition(void) { return "glitch"; }
static inline const char *get_example_for_output_transition_duration(void) { return "1000"; }
static inline const char *get_example_for_output_shader_speed(void) { return "2.0"; }
static inline const char *get_example_for_output_shader_fps(void) { return "30"; }
static inline const char *get_example_for_output_vsync(void) { return "false"; }
static inline const char *get_example_for_output_show_fps(void) { return "true"; }
static inline const char *get_example_for_output_channels(void) { return "[\"blue-noise\"]"; }

/* Metadata functions - default value */
static inline const char *get_default_for_default_path(void) { return "~/.local/share/neowall/default.png"; }
static inline const char *get_default_for_default_shader(void) { return ""; }
static inline const char *get_default_for_default_mode(void) { return "fill"; }
static inline const char *get_default_for_default_duration(void) { return "0"; }
static inline const char *get_default_for_default_transition(void) { return "fade"; }
static inline const char *get_default_for_default_transition_duration(void) { return "300"; }
static inline const char *get_default_for_default_shader_speed(void) { return "1.0"; }
static inline const char *get_default_for_default_shader_fps(void) { return "60"; }
static inline const char *get_default_for_default_vsync(void) { return "false"; }
static inline const char *get_default_for_default_show_fps(void) { return "false"; }
static inline const char *get_default_for_default_channels(void) { return ""; }
static inline const char *get_default_for_output_path(void) { return ""; }
static inline const char *get_default_for_output_shader(void) { return ""; }
static inline const char *get_default_for_output_mode(void) { return "fill"; }
static inline const char *get_default_for_output_duration(void) { return "0"; }
static inline const char *get_default_for_output_transition(void) { return "fade"; }
static inline const char *get_default_for_output_transition_duration(void) { return "300"; }
static inline const char *get_default_for_output_shader_speed(void) { return "1.0"; }
static inline const char *get_default_for_output_shader_fps(void) { return "60"; }
static inline const char *get_default_for_output_vsync(void) { return "false"; }
static inline const char *get_default_for_output_show_fps(void) { return "false"; }
static inline const char *get_default_for_output_channels(void) { return ""; }

/* Metadata functions - constraints */
static inline config_constraints_t get_constraints_for_default_path(void) {
    return (config_constraints_t){.path_constraints = {.must_exist = false, .is_directory = false}};
}
static inline config_constraints_t get_constraints_for_default_shader(void) {
    return (config_constraints_t){.path_constraints = {.must_exist = false, .is_directory = false}};
}
static inline config_constraints_t get_constraints_for_default_mode(void) {
    return (config_constraints_t){.enum_values = {.values = mode_enum_values, .count = 5}};
}
static inline config_constraints_t get_constraints_for_default_duration(void) {
    return (config_constraints_t){.int_range = {.min = 0, .max = 86400}};
}
static inline config_constraints_t get_constraints_for_default_transition(void) {
    return (config_constraints_t){.enum_values = {.values = transition_enum_values, .count = 6}};
}
static inline config_constraints_t get_constraints_for_default_transition_duration(void) {
    return (config_constraints_t){.int_range = {.min = 100, .max = 5000}};
}
static inline config_constraints_t get_constraints_for_default_shader_speed(void) {
    return (config_constraints_t){.float_range = {.min = 0.1, .max = 10.0}};
}
static inline config_constraints_t get_constraints_for_default_shader_fps(void) {
    return (config_constraints_t){.int_range = {.min = 1, .max = 240}};
}
static inline config_constraints_t get_constraints_for_default_vsync(void) {
    return (config_constraints_t){0};
}
static inline config_constraints_t get_constraints_for_default_show_fps(void) {
    return (config_constraints_t){0};
}
static inline config_constraints_t get_constraints_for_default_channels(void) {
    return (config_constraints_t){0};
}
static inline config_constraints_t get_constraints_for_output_path(void) {
    return (config_constraints_t){.path_constraints = {.must_exist = false, .is_directory = false}};
}
static inline config_constraints_t get_constraints_for_output_shader(void) {
    return (config_constraints_t){.path_constraints = {.must_exist = false, .is_directory = false}};
}
static inline config_constraints_t get_constraints_for_output_mode(void) {
    return (config_constraints_t){.enum_values = {.values = mode_enum_values, .count = 5}};
}
static inline config_constraints_t get_constraints_for_output_duration(void) {
    return (config_constraints_t){.int_range = {.min = 0, .max = 86400}};
}
static inline config_constraints_t get_constraints_for_output_transition(void) {
    return (config_constraints_t){.enum_values = {.values = transition_enum_values, .count = 6}};
}
static inline config_constraints_t get_constraints_for_output_transition_duration(void) {
    return (config_constraints_t){.int_range = {.min = 100, .max = 5000}};
}
static inline config_constraints_t get_constraints_for_output_shader_speed(void) {
    return (config_constraints_t){.float_range = {.min = 0.1, .max = 10.0}};
}
static inline config_constraints_t get_constraints_for_output_shader_fps(void) {
    return (config_constraints_t){.int_range = {.min = 1, .max = 240}};
}
static inline config_constraints_t get_constraints_for_output_vsync(void) {
    return (config_constraints_t){0};
}
static inline config_constraints_t get_constraints_for_output_show_fps(void) {
    return (config_constraints_t){0};
}
static inline config_constraints_t get_constraints_for_output_channels(void) {
    return (config_constraints_t){0};
}

/* Metadata functions - apply function pointers */
static inline bool (*get_apply_for_default_path(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_shader(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_mode(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_duration(void))(struct neowall_state *, const char *) { return apply_cycle_interval; }
static inline bool (*get_apply_for_default_transition(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_transition_duration(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_shader_speed(void))(struct neowall_state *, const char *) { return apply_shader_speed; }
static inline bool (*get_apply_for_default_shader_fps(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_vsync(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_show_fps(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_default_channels(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_path(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_shader(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_mode(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_duration(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_transition(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_transition_duration(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_shader_speed(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_shader_fps(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_vsync(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_show_fps(void))(struct neowall_state *, const char *) { return NULL; }
static inline bool (*get_apply_for_output_channels(void))(struct neowall_state *, const char *) { return NULL; }

/* Metadata functions - apply_output function pointers */
static inline bool (*get_apply_output_for_default_path(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_default_shader(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_default_mode(void))(struct output_state *, const char *) { return apply_output_mode; }
static inline bool (*get_apply_output_for_default_duration(void))(struct output_state *, const char *) { return apply_output_cycle_interval; }
static inline bool (*get_apply_output_for_default_transition(void))(struct output_state *, const char *) { return apply_output_transition; }
static inline bool (*get_apply_output_for_default_transition_duration(void))(struct output_state *, const char *) { return apply_output_transition_duration; }
static inline bool (*get_apply_output_for_default_shader_speed(void))(struct output_state *, const char *) { return apply_output_shader_speed; }
static inline bool (*get_apply_output_for_default_shader_fps(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_default_vsync(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_default_show_fps(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_default_channels(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_output_path(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_output_shader(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_output_mode(void))(struct output_state *, const char *) { return apply_output_mode; }
static inline bool (*get_apply_output_for_output_duration(void))(struct output_state *, const char *) { return apply_output_cycle_interval; }
static inline bool (*get_apply_output_for_output_transition(void))(struct output_state *, const char *) { return apply_output_transition; }
static inline bool (*get_apply_output_for_output_transition_duration(void))(struct output_state *, const char *) { return apply_output_transition_duration; }
static inline bool (*get_apply_output_for_output_shader_speed(void))(struct output_state *, const char *) { return apply_output_shader_speed; }
static inline bool (*get_apply_output_for_output_shader_fps(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_output_vsync(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_output_show_fps(void))(struct output_state *, const char *) { return NULL; }
static inline bool (*get_apply_output_for_output_channels(void))(struct output_state *, const char *) { return NULL; }

/* The configuration registry array */
static const config_key_info_t config_keys[] = {
    CONFIG_KEYS_LIST(CONFIG_ENTRY)
    {NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, {0}, NULL, NULL, NULL} /* Sentinel */
};

#undef CONFIG_ENTRY

/* ============================================================================
 * Schema Access Functions
 * ============================================================================ */

const config_key_info_t *config_get_schema(void) {
    return config_keys;
}

size_t config_get_schema_count(void) {
    static size_t count = 0;
    if (count == 0) {
        while (config_keys[count].key != NULL) {
            count++;
        }
    }
    return count;
}

const config_key_info_t *config_lookup_key(const char *key) {
    if (!key) return NULL;

    for (size_t i = 0; config_keys[i].key != NULL; i++) {
        if (strcmp(config_keys[i].key, key) == 0) {
            return &config_keys[i];
        }
    }

    return NULL;
}

const config_key_info_t **config_lookup_section(const char *section, size_t *count) {
    if (!section || !count) return NULL;

    /* Count matching entries */
    size_t matches = 0;
    for (size_t i = 0; config_keys[i].key != NULL; i++) {
        if (strcmp(config_keys[i].section, section) == 0) {
            matches++;
        }
    }

    if (matches == 0) {
        *count = 0;
        return NULL;
    }

    /* Allocate result array */
    const config_key_info_t **result = calloc(matches + 1, sizeof(config_key_info_t *));
    if (!result) {
        *count = 0;
        return NULL;
    }

    /* Fill result array */
    size_t idx = 0;
    for (size_t i = 0; config_keys[i].key != NULL; i++) {
        if (strcmp(config_keys[i].section, section) == 0) {
            result[idx++] = &config_keys[i];
        }
    }
    result[idx] = NULL;

    *count = matches;
    return result;
return NULL;
}

/* ============================================================================
* Mutual Exclusivity Validation
* ============================================================================ */

/**
* Validate that path and shader are mutually exclusive
* Called when setting either path or shader to ensure only one is set
*/
static bool validate_path_shader_mutual_exclusivity(
struct neowall_state *state,
const char *output_name,
const char *key_being_set,
const char *value
) {
/* If clearing the value (empty string), always allow */
if (!value || value[0] == '\0') {
    return true;
}

/* Determine which key we're setting and which we need to check */
bool setting_path = (strstr(key_being_set, "path") != NULL);
bool setting_shader = (strstr(key_being_set, "shader") != NULL);

if (!setting_path && !setting_shader) {
    return true; /* Not a path/shader key, no validation needed */
}

/* For global (default) settings */
if (!output_name) {
    /* TODO: Check global default config when we store it in state */
    return true; /* For now, allow - will be validated at parse time */
}

/* For per-output settings - find the output */
pthread_rwlock_rdlock(&state->output_list_lock);

struct output_state *output = state->outputs;
while (output) {
    if (output->connector_name && strcmp(output->connector_name, output_name) == 0) {
        break;
    }
    output = output->next;
}

if (!output) {
    pthread_rwlock_unlock(&state->output_list_lock);
    return true; /* Output not found, will be created - validation at that time */
}

/* Check mutual exclusivity */
bool is_valid = true;
if (setting_path) {
    /* Setting path - ensure it's not already a shader type */
    if (output->config.type == WALLPAPER_SHADER) {
        log_error("Cannot set path: shader is already set for output %s. Use EITHER path OR shader, not both.", output_name);
        is_valid = false;
    }
} else if (setting_shader) {
    /* Setting shader - ensure path is not set */
    if (output->config.path[0] != '\0') {
        log_error("Cannot set shader: path is already set for output %s. Use EITHER path OR shader, not both.", output_name);
        is_valid = false;
    }
}

pthread_rwlock_unlock(&state->output_list_lock);
return is_valid;
}

/* ============================================================================
* Validation Functions
* ============================================================================ */

static config_validation_result_t validate_success(void) {
    config_validation_result_t result;
    result.valid = true;
    result.error_message[0] = '\0';
    return result;
}

static config_validation_result_t validate_error(const char *fmt, ...) {
    config_validation_result_t result;
    result.valid = false;
    va_list args;
    va_start(args, fmt);
    vsnprintf(result.error_message, sizeof(result.error_message), fmt, args);
    va_end(args);
    return result;
}

config_validation_result_t config_validate_value(const char *key, const char *value) {
    if (!key || !value) {
        return validate_error("Key and value cannot be NULL");
    }

    const config_key_info_t *info = config_lookup_key(key);
    if (!info) {
        return validate_error("Unknown config key: %s", key);
    }

    /* Custom validator takes precedence */
    if (info->validate) {
        return info->validate(value);
    }

    /* Type-specific validation */
    switch (info->type) {
        case CONFIG_TYPE_INTEGER: {
            int64_t val;
            if (!config_parse_int(value, &val)) {
                return validate_error("Invalid integer: %s", value);
            }
            if (info->constraints.int_range.min != 0 || info->constraints.int_range.max != 0) {
                if (val < info->constraints.int_range.min || val > info->constraints.int_range.max) {
                    return validate_error("Value %ld out of range [%ld, %ld]",
                                        val, info->constraints.int_range.min, info->constraints.int_range.max);
                }
            }
            break;
        }

        case CONFIG_TYPE_FLOAT: {
            double val;
            if (!config_parse_float(value, &val)) {
                return validate_error("Invalid float: %s", value);
            }
            if (info->constraints.float_range.min != 0.0 || info->constraints.float_range.max != 0.0) {
                if (val < info->constraints.float_range.min || val > info->constraints.float_range.max) {
                    return validate_error("Value %.2f out of range [%.2f, %.2f]",
                                        val, info->constraints.float_range.min, info->constraints.float_range.max);
                }
            }
            break;
        }

        case CONFIG_TYPE_BOOLEAN: {
            bool val;
            if (!config_parse_bool(value, &val)) {
                return validate_error("Invalid boolean: %s (expected true/false, yes/no, on/off, 1/0)", value);
            }
            break;
        }

        case CONFIG_TYPE_ENUM: {
            bool found = false;
            for (size_t i = 0; i < info->constraints.enum_values.count; i++) {
                if (strcmp(value, info->constraints.enum_values.values[i]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                char allowed[512] = {0};
                for (size_t i = 0; i < info->constraints.enum_values.count; i++) {
                    if (i > 0) strcat(allowed, ", ");
                    strcat(allowed, info->constraints.enum_values.values[i]);
                }
                return validate_error("Invalid value '%s' (allowed: %s)", value, allowed);
            }
            break;
        }

        case CONFIG_TYPE_PATH: {
            /* Special validation for path/shader mutual exclusivity */
            if (strcmp(info->name, "path") == 0 || strcmp(info->name, "shader") == 0) {
                /* This will be validated in config_set_value with actual state */
                /* Here we just validate the path format itself */
            }

            /* Expand ~ to home directory */
            char expanded[4096];
            if (value[0] == '~') {
                const char *home = getenv("HOME");
                if (!home) {
                    return validate_error("Cannot expand ~: HOME not set");
                }
                snprintf(expanded, sizeof(expanded), "%s%s", home, value + 1);
            } else {
                snprintf(expanded, sizeof(expanded), "%s", value);
            }

            if (info->constraints.path_constraints.must_exist) {
                struct stat st;
                if (stat(expanded, &st) != 0) {
                    return validate_error("Path does not exist: %s", value);
                }
                if (info->constraints.path_constraints.is_directory && !S_ISDIR(st.st_mode)) {
                    return validate_error("Path is not a directory: %s", value);
                }
            }
            break;
        }

        case CONFIG_TYPE_STRING:
            /* String always valid unless custom validator says otherwise */
            break;
    }

    return validate_success();
}

/* ============================================================================
 * Type Conversion Functions
 * ============================================================================ */

bool config_parse_int(const char *str, int64_t *out) {
    if (!str || !out) return false;

    char *endptr;
    errno = 0;
    long long val = strtoll(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }

    *out = (int64_t)val;
    return true;
}

bool config_parse_float(const char *str, double *out) {
    if (!str || !out) return false;

    char *endptr;
    errno = 0;
    double val = strtod(str, &endptr);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }

    *out = val;
    return true;
}

bool config_parse_bool(const char *str, bool *out) {
    if (!str || !out) return false;

    /* Case-insensitive comparison */
    char lower[32];
    size_t len = strlen(str);
    if (len >= sizeof(lower)) return false;

    for (size_t i = 0; i < len; i++) {
        lower[i] = tolower(str[i]);
    }
    lower[len] = '\0';

    if (strcmp(lower, "true") == 0 || strcmp(lower, "yes") == 0 ||
        strcmp(lower, "on") == 0 || strcmp(lower, "1") == 0) {
        *out = true;
        return true;
    }

    if (strcmp(lower, "false") == 0 || strcmp(lower, "no") == 0 ||
        strcmp(lower, "off") == 0 || strcmp(lower, "0") == 0) {
        *out = false;
        return true;
    }

    return false;
}

void config_format_value(const config_key_info_t *key, const char *value,
                        char *buf, size_t buf_size) {
    if (!key || !value || !buf || buf_size == 0) return;

    switch (key->type) {
        case CONFIG_TYPE_INTEGER:
        case CONFIG_TYPE_FLOAT:
        case CONFIG_TYPE_BOOLEAN:
        case CONFIG_TYPE_STRING:
        case CONFIG_TYPE_ENUM:
        case CONFIG_TYPE_PATH:
            snprintf(buf, buf_size, "%s", value);
            break;
    }
}

/* ============================================================================
 * Default Value Functions
 * ============================================================================ */

const char *config_get_default(const char *key) {
    const config_key_info_t *info = config_lookup_key(key);
    return info ? info->default_value : NULL;
}

bool config_reset_to_default(struct neowall_state *state, const char *key) {
    if (!state) return false;

    if (key) {
        /* Reset specific key */
        const config_key_info_t *info = config_lookup_key(key);
        if (!info) return false;

        /* Apply default value */
        if (info->apply) {
            return info->apply(state, info->default_value);
        }
        return true;
    } else {
        /* Reset all keys */
        for (size_t i = 0; config_keys[i].key != NULL; i++) {
            if (config_keys[i].apply) {
                config_keys[i].apply(state, config_keys[i].default_value);
            }
        }
        return true;
    }
}

/* ============================================================================
 * Introspection Functions
 * ============================================================================ */

size_t config_list_all_keys(const char **keys, size_t max_keys) {
    if (!keys || max_keys == 0) return 0;

    size_t count = 0;
    for (size_t i = 0; config_keys[i].key != NULL && count < max_keys; i++) {
        keys[count++] = config_keys[i].key;
    }

    return count;
}

size_t config_list_sections(const char **sections, size_t max_sections) {
    if (!sections || max_sections == 0) return 0;

    size_t count = 0;
    const char *last_section = NULL;

    for (size_t i = 0; config_keys[i].key != NULL && count < max_sections; i++) {
        /* Only add unique sections */
        if (!last_section || strcmp(config_keys[i].section, last_section) != 0) {
            sections[count++] = config_keys[i].section;
            last_section = config_keys[i].section;
        }
    }

    return count;
}

char *config_export_schema_json(void) {
    /* Simple JSON export */
    size_t buf_size = 64 * 1024;
    char *json = malloc(buf_size);
    if (!json) return NULL;

    size_t offset = 0;
    offset += snprintf(json + offset, buf_size - offset, "{\n  \"keys\": [\n");

    for (size_t i = 0; config_keys[i].key != NULL; i++) {
        const config_key_info_t *key = &config_keys[i];

        if (i > 0) {
            offset += snprintf(json + offset, buf_size - offset, ",\n");
        }

        const char *type_str =
            key->type == CONFIG_TYPE_INTEGER ? "integer" :
            key->type == CONFIG_TYPE_FLOAT ? "float" :
            key->type == CONFIG_TYPE_BOOLEAN ? "boolean" :
            key->type == CONFIG_TYPE_STRING ? "string" :
            key->type == CONFIG_TYPE_ENUM ? "enum" : "path";

        const char *scope_str =
            key->scope == CONFIG_SCOPE_GLOBAL ? "global" :
            key->scope == CONFIG_SCOPE_OUTPUT ? "output" : "both";

        offset += snprintf(json + offset, buf_size - offset,
            "    {\n"
            "      \"key\": \"%s\",\n"
            "      \"section\": \"%s\",\n"
            "      \"name\": \"%s\",\n"
            "      \"type\": \"%s\",\n"
            "      \"scope\": \"%s\",\n"
            "      \"description\": \"%s\",\n"
            "      \"default\": \"%s\"\n"
            "    }",
            key->key,
            key->section,
            key->name,
            type_str,
            scope_str,
            key->description,
            key->default_value);
    }

    offset += snprintf(json + offset, buf_size - offset, "\n  ]\n}\n");

    return json;
}

/* ============================================================================
 * Apply Functions - Runtime updates
 * ============================================================================ */

static bool apply_cycle_interval(struct neowall_state *state, const char *value) {
    int64_t interval;
    if (!config_parse_int(value, &interval)) return false;

    log_info("Global cycle interval set to %ld seconds", interval);
    return true;
}

static bool apply_shader_speed(struct neowall_state *state, const char *value) {
    double speed;
    if (!config_parse_float(value, &speed)) return false;

    atomic_store(&state->shader_speed, (float)speed);
    log_info("Global shader speed set to %.2f", speed);
    return true;
}

static bool apply_output_cycle_interval(struct output_state *output, const char *value) {
    int64_t interval;
    if (!config_parse_int(value, &interval)) return false;

    output->config.duration = (float)interval;
    log_info("Output %s cycle interval set to %ld seconds",
             output->connector_name, interval);
    return true;
}

static bool apply_output_mode(struct output_state *output, const char *value) {
    enum wallpaper_mode mode;

    if (strcmp(value, "center") == 0) mode = MODE_CENTER;
    else if (strcmp(value, "stretch") == 0) mode = MODE_STRETCH;
    else if (strcmp(value, "fit") == 0) mode = MODE_FIT;
    else if (strcmp(value, "fill") == 0) mode = MODE_FILL;
    else if (strcmp(value, "tile") == 0) mode = MODE_TILE;
    else return false;

    output->config.mode = mode;
    output->needs_redraw = true;

    log_info("Output %s wallpaper mode set to %s",
             output->connector_name, value);
    return true;
}

static bool apply_output_shader_speed(struct output_state *output, const char *value) {
    double speed;
    if (!config_parse_float(value, &speed)) return false;

    output->config.shader_speed = (float)speed;
    log_info("Output %s shader speed set to %.2f",
             output->connector_name, speed);
    return true;
}

static bool apply_output_transition(struct output_state *output, const char *value) {
    enum transition_type type;

    if (strcmp(value, "none") == 0) type = TRANSITION_NONE;
    else if (strcmp(value, "fade") == 0) type = TRANSITION_FADE;
    else if (strcmp(value, "slide_left") == 0 || strcmp(value, "slide-left") == 0) type = TRANSITION_SLIDE_LEFT;
    else if (strcmp(value, "slide_right") == 0 || strcmp(value, "slide-right") == 0) type = TRANSITION_SLIDE_RIGHT;
    else if (strcmp(value, "glitch") == 0) type = TRANSITION_GLITCH;
    else if (strcmp(value, "pixelate") == 0) type = TRANSITION_PIXELATE;
    else return false;

    output->config.transition = type;
    log_info("Output %s transition set to %s", output->connector_name, value);
    return true;
}

static bool apply_output_transition_duration(struct output_state *output, const char *value) {
    int64_t duration_ms;
    if (!config_parse_int(value, &duration_ms)) return false;

    output->config.transition_duration = (float)duration_ms / 1000.0f;
    log_info("Output %s transition duration set to %ld ms",
             output->connector_name, duration_ms);
    return true;
}
