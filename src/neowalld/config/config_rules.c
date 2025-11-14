/*
 * Configuration Rules Implementation
 * Extensible validation system for config key relationships
 */

#include "config_rules.h"
#include "config_keys.h"
#include "neowall.h"
#include "output/output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * RULES REGISTRY - GENERATED FROM config_rules.h
 * ============================================================================ */

/* Helper macro to expand rule definitions */
#define EXPAND_RULE(rule) rule,

/* The complete rules array */
static const config_rule_t config_rules[] = {
    CONFIG_RULES_LIST(EXPAND_RULE)
    {0, NULL, NULL, 0, NULL} /* Sentinel */
};

#undef EXPAND_RULE

/* ============================================================================
 * WALLPAPER TYPE DETECTION
 * ============================================================================ */

wallpaper_type_t config_get_wallpaper_type(
    struct neowall_state *state,
    const char *output_name
) {
    if (!state) return WALLPAPER_TYPE_ANY;

    /* For global default, check if it would use path or shader */
    if (!output_name) {
        /* TODO: Check global default config when stored in state */
        return WALLPAPER_TYPE_ANY;
    }

    /* For specific output, check actual configuration */
    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    while (output) {
        if (output->connector_name[0] != '\0' && strcmp(output->connector_name, output_name) == 0) {
            break;
        }
        output = output->next;
    }

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        return WALLPAPER_TYPE_ANY; /* Output not found yet */
    }

    /* Determine type based on what's set */
    wallpaper_type_t type = WALLPAPER_TYPE_ANY;

    if (output->config.type == WALLPAPER_SHADER) {
        type = WALLPAPER_TYPE_SHADER;
    } else if (output->config.path[0] != '\0') {
        type = WALLPAPER_TYPE_STATIC;
    }

    pthread_rwlock_unlock(&state->output_list_lock);
    return type;
}

/* ============================================================================
 * KEY APPLICABILITY CHECKS
 * ============================================================================ */

bool config_is_key_applicable(
    const char *key,
    wallpaper_type_t wallpaper_type
) {
    if (!key) return false;

    /* Extract just the key name (after section.) */
    const char *key_name = strrchr(key, '.');
    if (key_name) {
        key_name++; /* Skip the dot */
    } else {
        key_name = key;
    }

    /* Universal keys (work with any type) */
    if (IS_UNIVERSAL_KEY(key_name)) {
        return true;
    }

    /* Static-only keys */
    if (IS_STATIC_ONLY_KEY(key_name)) {
        return wallpaper_type == WALLPAPER_TYPE_STATIC ||
               wallpaper_type == WALLPAPER_TYPE_ANY;
    }

    /* Shader-only keys */
    if (IS_SHADER_ONLY_KEY(key_name)) {
        return wallpaper_type == WALLPAPER_TYPE_SHADER ||
               wallpaper_type == WALLPAPER_TYPE_ANY;
    }

    /* Type selector keys (path/shader) */
    if (IS_TYPE_SELECTOR_KEY(key_name)) {
        return true; /* These define the type */
    }

    /* Default: not recognized, assume applicable */
    return true;
}

/* ============================================================================
 * RULE VALIDATION
 * ============================================================================ */

static bool check_mutual_exclusive_rule(
    struct neowall_state *state,
    const char *output_name,
    const config_rule_t *rule,
    const char *key,
    const char *value,
    char *error_buf,
    size_t error_len
) {
    /* Extract key names without section prefix */
    const char *key_name = strrchr(key, '.');
    key_name = key_name ? key_name + 1 : key;

    /* Check if we're setting one of the mutually exclusive keys */
    bool setting_key1 = (strcmp(key_name, rule->key1) == 0);
    bool setting_key2 = (strcmp(key_name, rule->key2) == 0);

    if (!setting_key1 && !setting_key2) {
        return true; /* Not relevant to this rule */
    }

    /* If clearing the value (empty string), always allow */
    if (!value || value[0] == '\0') {
        return true;
    }

    /* Check if the other key is already set */
    if (!output_name) {
        /* Global default - TODO: check when we store global config */
        return true;
    }

    /* Check specific output */
    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    while (output) {
        if (output->connector_name[0] != '\0' && strcmp(output->connector_name, output_name) == 0) {
            break;
        }
        output = output->next;
    }

    if (!output) {
        pthread_rwlock_unlock(&state->output_list_lock);
        return true; /* Output not found yet, will be created */
    }

    bool conflict = false;

    if (setting_key1 && strcmp(rule->key2, "shader") == 0) {
        /* Setting path, check if shader is set */
        if (output->config.type == WALLPAPER_SHADER) {
            conflict = true;
        }
    } else if (setting_key2 && strcmp(rule->key1, "path") == 0) {
        /* Setting shader, check if path is set */
        if (output->config.path[0] != '\0') {
            conflict = true;
        }
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    if (conflict) {
        if (error_buf && error_len > 0) {
            snprintf(error_buf, error_len, "%s", rule->error_message);
        }
        return false;
    }

    return true;
}

static bool check_only_with_type_rule(
    struct neowall_state *state,
    const char *output_name,
    const config_rule_t *rule,
    const char *key,
    const char *value,
    char *error_buf,
    size_t error_len
) {
    /* Extract key name without section prefix */
    const char *key_name = strrchr(key, '.');
    key_name = key_name ? key_name + 1 : key;

    /* Check if we're setting a type-specific key */
    if (strcmp(key_name, rule->key1) != 0) {
        return true; /* Not the key this rule applies to */
    }

    /* If clearing the value, always allow */
    if (!value || value[0] == '\0') {
        return true;
    }

    /* Get current wallpaper type */
    wallpaper_type_t current_type = config_get_wallpaper_type(state, output_name);

    /* Check if key is applicable to current type */
    bool is_applicable = false;

    if (current_type == WALLPAPER_TYPE_ANY) {
        /* Type not determined yet, allow for now */
        return true;
    }

    if (rule->applies_to == WALLPAPER_TYPE_STATIC && current_type == WALLPAPER_TYPE_STATIC) {
        is_applicable = true;
    } else if (rule->applies_to == WALLPAPER_TYPE_SHADER && current_type == WALLPAPER_TYPE_SHADER) {
        is_applicable = true;
    } else if (rule->applies_to == WALLPAPER_TYPE_ANY) {
        is_applicable = true;
    }

    if (!is_applicable) {
        if (error_buf && error_len > 0) {
            snprintf(error_buf, error_len, "%s", rule->error_message);
        }
        return false;
    }

    return true;
}

bool config_validate_rules(
    struct neowall_state *state,
    const char *output_name,
    const char *key,
    const char *value,
    char *error_buf,
    size_t error_len
) {
    if (!state || !key) {
        return true; /* Nothing to validate */
    }

    /* Check all rules */
    for (size_t i = 0; config_rules[i].key1 != NULL; i++) {
        const config_rule_t *rule = &config_rules[i];
        bool valid = true;

        switch (rule->type) {
            case RULE_TYPE_MUTUAL_EXCLUSIVE:
                valid = check_mutual_exclusive_rule(state, output_name, rule, key, value, error_buf, error_len);
                break;

            case RULE_TYPE_ONLY_WITH_TYPE:
                valid = check_only_with_type_rule(state, output_name, rule, key, value, error_buf, error_len);
                break;

            case RULE_TYPE_REQUIRES:
            case RULE_TYPE_CONFLICTS_WITH:
                /* TODO: Implement if needed */
                valid = true;
                break;
        }

        if (!valid) {
            return false;
        }
    }

    return true;
}

/* ============================================================================
 * INTROSPECTION API
 * ============================================================================ */

const config_rule_t *config_get_all_rules(void) {
    return config_rules;
}

void config_explain_key_rules(
    const char *key,
    char *buf,
    size_t buf_size
) {
    if (!key || !buf || buf_size == 0) return;

    /* Extract key name without section prefix */
    const char *key_name = strrchr(key, '.');
    key_name = key_name ? key_name + 1 : key;

    size_t offset = 0;
    offset += snprintf(buf + offset, buf_size - offset, "%s:\n", key_name);

    /* Find all rules that apply to this key */
    bool found_any = false;

    for (size_t i = 0; config_rules[i].key1 != NULL; i++) {
        const config_rule_t *rule = &config_rules[i];

        if (strcmp(rule->key1, key_name) == 0 || strcmp(rule->key2, key_name) == 0) {
            found_any = true;
            offset += snprintf(buf + offset, buf_size - offset, "  - %s\n", rule->error_message);
        }
    }

    if (!found_any) {
        offset += snprintf(buf + offset, buf_size - offset, "  - No special restrictions\n");
    }

    /* Add type-specific information */
    if (IS_STATIC_ONLY_KEY(key_name)) {
        offset += snprintf(buf + offset, buf_size - offset,
                          "\nApplies to: Static wallpapers only (when using 'path')\n");
    } else if (IS_SHADER_ONLY_KEY(key_name)) {
        offset += snprintf(buf + offset, buf_size - offset,
                          "\nApplies to: Shader wallpapers only (when using 'shader')\n");
    } else if (IS_TYPE_SELECTOR_KEY(key_name)) {
        offset += snprintf(buf + offset, buf_size - offset,
                          "\nDetermines wallpaper type: Choose EITHER path OR shader\n");
    } else if (IS_UNIVERSAL_KEY(key_name)) {
        offset += snprintf(buf + offset, buf_size - offset,
                          "\nApplies to: Both static and shader wallpapers\n");
    }
}

/* ============================================================================
 * KEY LISTING API
 * ============================================================================ */

size_t config_get_applicable_keys(
    wallpaper_type_t wallpaper_type,
    const char **keys,
    size_t max_keys
) {
    if (!keys || max_keys == 0) return 0;

    size_t count = 0;

    /* Helper macro to add key if applicable */
    #define ADD_KEY(key_name) \
        if (count < max_keys && config_is_key_applicable(key_name, wallpaper_type)) { \
            keys[count++] = key_name; \
        }

    /* Type selector keys (always include these) */
    ADD_KEY("path");
    ADD_KEY("shader");

    /* Static-only keys */
    if (wallpaper_type == WALLPAPER_TYPE_STATIC || wallpaper_type == WALLPAPER_TYPE_ANY) {
        ADD_KEY("mode");
        ADD_KEY("transition");
        ADD_KEY("transition_duration");
        ADD_KEY("duration");
    }

    /* Shader-only keys */
    if (wallpaper_type == WALLPAPER_TYPE_SHADER || wallpaper_type == WALLPAPER_TYPE_ANY) {
        ADD_KEY("shader_fps");
        ADD_KEY("vsync");
        ADD_KEY("channels");
        ADD_KEY("show_fps");
    }

    /* Universal keys (work for both) */
    ADD_KEY("duration");

    #undef ADD_KEY

    return count;
}
