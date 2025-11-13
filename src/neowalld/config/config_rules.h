#ifndef CONFIG_RULES_H
#define CONFIG_RULES_H

#include <stdbool.h>
#include <stddef.h>

/*
 * ============================================================================
 * CONFIGURATION VALIDATION RULES - SINGLE SOURCE OF TRUTH
 * ============================================================================
 * 
 * This file defines EXPLICIT RULES about which config keys can be used together.
 * These rules ensure logical consistency and prevent invalid configurations.
 * 
 * KEY PRINCIPLE: Static wallpapers and Shaders are FUNDAMENTALLY DIFFERENT
 * -------------------------------------------------------------------------
 * - Static wallpapers use 'path' and support: mode, transition, transition_duration, duration
 * - Shaders use 'shader' and support: shader_speed, shader_fps, vsync, channels
 * - Some settings like show_fps work for both types
 * 
 * Rule Types:
 * 1. Mutual Exclusivity - Keys that cannot be used together
 * 2. Type-specific Keys - Keys that only apply to certain wallpaper types
 * 3. Conditional Requirements - Keys that require other keys to be set
 * 4. Conditional Restrictions - Keys that are only valid with certain other keys
 * 
 * To add a new rule:
 * 1. Add entry to the appropriate RULES_LIST macro
 * 2. Update the applicability tables if needed
 * 3. Validation is automatically applied
 * 
 * ============================================================================
 */

/* Forward declarations */
struct neowall_state;
struct output_state;

/* ============================================================================
 * Wallpaper Type Classification
 * ============================================================================ */

typedef enum {
    WALLPAPER_TYPE_STATIC,    /* Static image wallpaper (uses 'path') */
    WALLPAPER_TYPE_SHADER,    /* Shader wallpaper (uses 'shader') */
    WALLPAPER_TYPE_ANY,       /* Applies to both types */
} wallpaper_type_t;

/* ============================================================================
 * Rule Types
 * ============================================================================ */

typedef enum {
    RULE_TYPE_MUTUAL_EXCLUSIVE,    /* Keys cannot both be set */
    RULE_TYPE_ONLY_WITH_TYPE,      /* Key only valid with specific wallpaper type */
    RULE_TYPE_REQUIRES,            /* Key A requires key B to be set */
    RULE_TYPE_CONFLICTS_WITH,      /* Key A conflicts with key B when both non-empty */
} config_rule_type_t;

/* Rule definition */
typedef struct {
    config_rule_type_t type;
    const char *key1;           /* Primary key */
    const char *key2;           /* Secondary key (for relational rules) */
    wallpaper_type_t applies_to; /* Which wallpaper type this key applies to */
    const char *error_message;  /* User-friendly error message */
} config_rule_t;

/* ============================================================================
 * RULE DEFINITIONS - EXTENSIBLE AND EXPLICIT
 * ============================================================================ */

/*
 * RULE 1: PATH AND SHADER ARE MUTUALLY EXCLUSIVE (FUNDAMENTAL RULE)
 * ------------------------------------------------------------------
 * An output can use EITHER:
 *   - 'path' → static image/directory of images
 *   - 'shader' → animated GLSL shader
 * But NEVER both at the same time.
 * 
 * This is the most important rule - it determines everything else.
 */
#define RULE_PATH_SHADER_EXCLUSIVE \
    { \
        .type = RULE_TYPE_MUTUAL_EXCLUSIVE, \
        .key1 = "path", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_ANY, \
        .error_message = "Cannot use both 'path' and 'shader'. Choose EITHER static wallpaper (path) OR animated shader (shader), not both." \
    }

/*
 * ============================================================================
 * STATIC WALLPAPER ONLY RULES (only valid when using 'path')
 * ============================================================================
 */

/*
 * RULE 2: MODE - Display mode for static images
 * ----------------------------------------------
 * How to display the image: center, stretch, fit, fill, tile
 * Only applies to static images. Shaders always render fullscreen.
 */
#define RULE_MODE_STATIC_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "mode", \
        .key2 = "path", \
        .applies_to = WALLPAPER_TYPE_STATIC, \
        .error_message = "'mode' only applies to static wallpapers (path). Shaders always render fullscreen and don't use display modes." \
    }

/*
 * RULE 3: TRANSITION - Transition effects between images
 * -------------------------------------------------------
 * Visual effect when switching between wallpapers: fade, slide, glitch, etc.
 * Only applies to static images. Shaders are already animated continuously.
 */
#define RULE_TRANSITION_STATIC_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "transition", \
        .key2 = "path", \
        .applies_to = WALLPAPER_TYPE_STATIC, \
        .error_message = "'transition' only applies to static wallpapers (path). Shaders are already animated and don't use transitions." \
    }

/*
 * RULE 4: TRANSITION_DURATION - How long the transition takes
 * ------------------------------------------------------------
 * Duration in seconds for the transition effect.
 * Only applies to static images (requires transition to be meaningful).
 */
#define RULE_TRANSITION_DURATION_STATIC_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "transition_duration", \
        .key2 = "path", \
        .applies_to = WALLPAPER_TYPE_STATIC, \
        .error_message = "'transition_duration' only applies to static wallpapers (path). Shaders don't use transitions." \
    }

/*
 * RULE 5: DURATION - Wallpaper/shader cycling interval
 * -----------------------------------------------------
 * Time in seconds before switching to the next item in a directory.
 * Applies to BOTH:
 *  - Static images: directory of images (path ending with /)
 *  - Shaders: directory of shaders (shader ending with /)
 * 
 * Note: Individual shaders animate continuously, but directories of shaders can cycle.
 */
/* REMOVED - duration applies to both types when using directories */

/*
 * ============================================================================
 * SHADER ONLY RULES (only valid when using 'shader')
 * ============================================================================
 */

/*
 * RULE 6: SHADER_SPEED - Animation speed multiplier
 * --------------------------------------------------
 * Controls how fast the shader animation runs (multiplier applied to time uniform).
 * Only applies to shaders. Static images have no animation to speed up/slow down.
 */
#define RULE_SHADER_SPEED_SHADER_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "shader_speed", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_SHADER, \
        .error_message = "'shader_speed' only applies to shader wallpapers (shader). Static images (path) have no animation speed." \
    }

/*
 * RULE 7: SHADER_FPS - Target framerate for shader rendering
 * -----------------------------------------------------------
 * Sets the target FPS for shader animation (default 60).
 * Only applies to shaders. Static images don't need continuous rendering.
 */
#define RULE_SHADER_FPS_SHADER_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "shader_fps", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_SHADER, \
        .error_message = "'shader_fps' only applies to shader wallpapers (shader). Static images (path) don't need FPS control." \
    }

/*
 * RULE 8: VSYNC - Vertical sync for shader rendering
 * ---------------------------------------------------
 * Syncs shader rendering to monitor refresh rate.
 * Only applies to shaders. Static images are rendered once and don't need vsync.
 */
#define RULE_VSYNC_SHADER_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "vsync", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_SHADER, \
        .error_message = "'vsync' only applies to shader wallpapers (shader). Static images (path) don't need vsync." \
    }

/*
 * RULE 9: CHANNELS - Shader texture inputs (iChannel0-3)
 * -------------------------------------------------------
 * Array of texture paths/names for Shadertoy-compatible iChannel uniforms.
 * Only applies to shaders. Static images don't use shader uniforms.
 */
#define RULE_CHANNELS_SHADER_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "channels", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_SHADER, \
        .error_message = "'channels' (iChannel textures) only apply to shader wallpapers (shader). Static images (path) don't use shader uniforms." \
    }

/*
 * RULE 10: SHOW_FPS - FPS counter display
 * ----------------------------------------
 * Shows FPS counter on screen during shader animation.
 * Only applies to shaders. Static images are rendered once and don't have meaningful FPS.
 */
#define RULE_SHOW_FPS_SHADER_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "show_fps", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_SHADER, \
        .error_message = "'show_fps' only applies to shader wallpapers (shader). Static images (path) don't animate and have no FPS to display." \
    }

/*
 * ============================================================================
 * UNIVERSAL RULES (apply to both static and shader wallpapers)
 * ============================================================================
 */

/*
 * DURATION - Cycling interval for directories
 * --------------------------------------------
 * Works with both directory of images (path/) and directory of shaders (shader/)
 */
/* No special rule needed - duration is explicitly allowed for both types */

/*
 * ============================================================================
 * ALL RULES LIST - EXTENSIBLE MACRO
 * ============================================================================
 * To add a new rule, add a line here: X(YOUR_NEW_RULE)
 */

#define CONFIG_RULES_LIST(X) \
    X(RULE_PATH_SHADER_EXCLUSIVE) \
    X(RULE_MODE_STATIC_ONLY) \
    X(RULE_TRANSITION_STATIC_ONLY) \
    X(RULE_TRANSITION_DURATION_STATIC_ONLY) \
    X(RULE_SHADER_SPEED_SHADER_ONLY) \
    X(RULE_SHADER_FPS_SHADER_ONLY) \
    X(RULE_VSYNC_SHADER_ONLY) \
    X(RULE_CHANNELS_SHADER_ONLY) \
    X(RULE_SHOW_FPS_SHADER_ONLY)

/*
 * ============================================================================
 * KEY APPLICABILITY TABLES - QUICK REFERENCE
 * ============================================================================
 */

/*
 * STATIC WALLPAPER KEYS (only valid with 'path'):
 * ------------------------------------------------
 * - mode                  : How to display the image
 * - transition            : Effect when changing wallpapers
 * - transition_duration   : How long the transition takes
 */
#define STATIC_ONLY_KEYS(X) \
    X("mode") \
    X("transition") \
    X("transition_duration")

/*
 * SHADER WALLPAPER KEYS (only valid with 'shader'):
 * --------------------------------------------------
 * - shader_speed          : Animation speed multiplier
 * - shader_fps            : Target framerate
 * - vsync                 : Sync to monitor refresh
 * - channels              : iChannel texture inputs
 * - show_fps              : FPS counter overlay
 */
#define SHADER_ONLY_KEYS(X) \
    X("shader_speed") \
    X("shader_fps") \
    X("vsync") \
    X("channels") \
    X("show_fps")

/*
 * UNIVERSAL KEYS (valid for both types):
 * ---------------------------------------
 * - path / shader         : Type selector (mutually exclusive)
 * - duration              : Cycling interval (for directories of images or shaders)
 */
#define UNIVERSAL_KEYS(X) \
    X("path") \
    X("shader") \
    X("duration")

/* ============================================================================
 * HELPER MACROS FOR COMMON PATTERNS
 * ============================================================================ */

/* Check if a key is static-wallpaper-only */
#define IS_STATIC_ONLY_KEY(key) \
    (strcmp(key, "mode") == 0 || \
     strcmp(key, "transition") == 0 || \
     strcmp(key, "transition_duration") == 0)

/* Check if a key is shader-only */
#define IS_SHADER_ONLY_KEY(key) \
    (strcmp(key, "shader_speed") == 0 || \
     strcmp(key, "shader_fps") == 0 || \
     strcmp(key, "vsync") == 0 || \
     strcmp(key, "channels") == 0 || \
     strcmp(key, "show_fps") == 0)

/* Check if a key determines wallpaper type */
#define IS_TYPE_SELECTOR_KEY(key) \
    (strcmp(key, "path") == 0 || strcmp(key, "shader") == 0)

/* Check if a key is universal (works with both types) */
#define IS_UNIVERSAL_KEY(key) \
    (strcmp(key, "duration") == 0 || \
     IS_TYPE_SELECTOR_KEY(key))

/* ============================================================================
 * RULE VALIDATION API
 * ============================================================================ */

/**
 * Validate that a key-value pair follows all configuration rules
 * 
 * @param state NeoWall state (to check existing config)
 * @param output_name Output name (NULL for global default)
 * @param key Config key being set
 * @param value Value being set
 * @param error_buf Buffer for error message
 * @param error_len Size of error buffer
 * @return true if valid, false if violates rules
 */
bool config_validate_rules(
    struct neowall_state *state,
    const char *output_name,
    const char *key,
    const char *value,
    char *error_buf,
    size_t error_len
);

/**
 * Get the current wallpaper type for an output
 * 
 * @param state NeoWall state
 * @param output_name Output name (NULL for global default)
 * @return Current wallpaper type (STATIC, SHADER, or ANY if undetermined)
 */
wallpaper_type_t config_get_wallpaper_type(
    struct neowall_state *state,
    const char *output_name
);

/**
 * Check if a key is applicable to the current wallpaper type
 * 
 * @param key Config key (without section prefix)
 * @param wallpaper_type Current wallpaper type
 * @return true if key is applicable to this type
 */
bool config_is_key_applicable(
    const char *key,
    wallpaper_type_t wallpaper_type
);

/**
 * Get list of all rules (for documentation/introspection)
 * 
 * @return Array of all config rules (NULL-terminated)
 */
const config_rule_t *config_get_all_rules(void);

/**
 * Get human-readable explanation of rules for a specific key
 * 
 * @param key Config key
 * @param buf Buffer to write explanation
 * @param buf_size Size of buffer
 */
void config_explain_key_rules(
    const char *key,
    char *buf,
    size_t buf_size
);

/**
 * Get list of keys applicable to a specific wallpaper type
 * 
 * @param wallpaper_type Type to get keys for
 * @param keys Output array of key names
 * @param max_keys Maximum number of keys to return
 * @return Number of keys returned
 */
size_t config_get_applicable_keys(
    wallpaper_type_t wallpaper_type,
    const char **keys,
    size_t max_keys
);

/* ============================================================================
 * USAGE EXAMPLES
 * ============================================================================ 
 * 
 * Example 1: Validate before setting a config value
 * --------------------------------------------------
 * ```c
 * char error[256];
 * if (!config_validate_rules(state, "DP-1", "shader_speed", "2.0", error, sizeof(error))) {
 *     log_error("Invalid config: %s", error);
 *     // Error: "'shader_speed' only applies to shader wallpapers (shader).
 *     //         Static images (path) have no animation speed."
 * }
 * ```
 * 
 * Example 2: Check if key is applicable to current type
 * ------------------------------------------------------
 * ```c
 * wallpaper_type_t type = config_get_wallpaper_type(state, "DP-1");
 * if (!config_is_key_applicable("transition", type)) {
 *     printf("Transition not available for shader wallpapers\n");
 * }
 * ```
 * 
 * Example 3: Show user what keys they can use
 * --------------------------------------------
 * ```c
 * char explanation[512];
 * config_explain_key_rules("shader_speed", explanation, sizeof(explanation));
 * printf("%s\n", explanation);
 * // Output:
 * // "shader_speed:
 * //  - 'shader_speed' only applies to shader wallpapers (shader).
 * //    Static images (path) have no animation speed.
 * //
 * // Applies to: Shader wallpapers only (when using 'shader')"
 * ```
 * 
 * Example 4: List applicable keys for UI
 * ---------------------------------------
 * ```c
 * const char *keys[32];
 * size_t count = config_get_applicable_keys(WALLPAPER_TYPE_SHADER, keys, 32);
 * printf("Shader settings:\n");
 * for (size_t i = 0; i < count; i++) {
 *     printf("  - %s\n", keys[i]);
 * }
 * // Output:
 * // "Shader settings:
 * //   - shader
 * //   - shader_speed
 * //   - shader_fps
 * //   - vsync
 * //   - channels
 * //   - show_fps"
 * ```
 * 
 * Example 5: Generate documentation automatically
 * ------------------------------------------------
 * ```c
 * const config_rule_t *rules = config_get_all_rules();
 * printf("Configuration Rules:\n");
 * printf("====================\n\n");
 * 
 * for (int i = 0; rules[i].key1 != NULL; i++) {
 *     printf("Rule %d: %s\n", i+1, rules[i].error_message);
 * }
 * ```
 * 
 * Example 6: Validate entire configuration
 * -----------------------------------------
 * ```c
 * // When user tries to set both path and shader:
 * if (!config_validate_rules(state, "DP-1", "shader", "plasma.glsl", error, sizeof(error))) {
 *     // If path is already set, error will be:
 *     // "Cannot use both 'path' and 'shader'. Choose EITHER static wallpaper (path)
 *     //  OR animated shader (shader), not both."
 * }
 * ```
 * 
 * ============================================================================
 * EXTENSIBILITY GUIDE
 * ============================================================================
 * 
 * To add a new shader-only setting (e.g., "shader_quality"):
 * 
 * 1. Add to config_keys.h:
 *    #define CONFIG_KEY_DEFAULT_SHADER_QUALITY "default.shader_quality"
 * 
 * 2. Add rule definition:
 *    #define RULE_SHADER_QUALITY_SHADER_ONLY \
 *        { .type = RULE_TYPE_ONLY_WITH_TYPE, \
 *          .key1 = "shader_quality", \
 *          .key2 = "shader", \
 *          .applies_to = WALLPAPER_TYPE_SHADER, \
 *          .error_message = "'shader_quality' only applies to shader wallpapers." }
 * 
 * 3. Add to CONFIG_RULES_LIST:
 *    X(RULE_SHADER_QUALITY_SHADER_ONLY)
 * 
 * 4. Add to SHADER_ONLY_KEYS:
 *    X("shader_quality")
 * 
 * 5. Update IS_SHADER_ONLY_KEY macro:
 *    || strcmp(key, "shader_quality") == 0
 * 
 * That's it! The validation system will automatically:
 * - Reject "shader_quality" when using 'path'
 * - Allow it when using 'shader'
 * - Include it in introspection queries
 * - Generate proper error messages
 * 
 * ============================================================================ */

#endif /* CONFIG_RULES_H */