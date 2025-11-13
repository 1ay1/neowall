/*
 * Atomic Configuration Writer
 * Handles safe, atomic writing of configuration to disk
 */

#include "config_write.h"
#include "config_schema.h"
#include "config_rules.h"
#include "vibe.h"
#include "neowall.h"
#include "output/output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>
#include <stdarg.h>
#include <pthread.h>

/* ============================================================================
 * ATOMIC WRITE IMPLEMENTATION
 * ============================================================================
 * Uses the rename() strategy for atomic writes:
 * 1. Write to temporary file (.tmp)
 * 2. Fsync the temp file
 * 3. Rename temp to actual file (atomic operation)
 * 4. Fsync the directory
 *
 * This ensures:
 * - No partial writes visible to readers
 * - Config is either old or new, never corrupted
 * - Survives power loss during write
 * ============================================================================ */

/* Maximum config file size (4MB should be plenty) */
#define MAX_CONFIG_SIZE (4 * 1024 * 1024)

/* Helper: Set error message */
static void set_error(char *buf, size_t len, const char *fmt, ...) {
    if (!buf || len == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, len, fmt, args);
    va_end(args);
}

/* Helper: Expand ~ in paths */
static void expand_path(const char *path, char *expanded, size_t expanded_len) {
    if (!path || !expanded || expanded_len == 0) return;

    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded, expanded_len, "%s%s", home, path + 1);
        } else {
            snprintf(expanded, expanded_len, "%s", path);
        }
    } else {
        snprintf(expanded, expanded_len, "%s", path);
    }
}

/* Helper: Fsync file and its directory */
static bool fsync_path(const char *path) {
    /* Fsync the file */
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;

    if (fsync(fd) != 0) {
        close(fd);
        return false;
    }
    close(fd);

    /* Fsync the directory (ensures directory entry is persisted) */
    char *path_copy = strdup(path);
    if (!path_copy) return false;

    char *dir = dirname(path_copy);
    fd = open(dir, O_RDONLY | O_DIRECTORY);
    free(path_copy);

    if (fd < 0) return true; /* Not fatal if we can't fsync dir */

    fsync(fd);
    close(fd);

    return true;
}

/* ============================================================================
 * BACKUP MANAGEMENT
 * ============================================================================ */

bool config_create_backup(const char *config_path) {
    if (!config_path) return false;

    /* Check if config file exists */
    if (access(config_path, F_OK) != 0) {
        return true; /* No file to backup */
    }

    /* Create backup path with timestamp */
    char backup_path[4096];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    snprintf(backup_path, sizeof(backup_path), "%s.backup.%04d%02d%02d_%02d%02d%02d",
             config_path,
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec);

    /* Also keep a simple .backup for easy restoration */
    char simple_backup[4096];
    snprintf(simple_backup, sizeof(simple_backup), "%s.backup", config_path);

    /* Copy file */
    FILE *src = fopen(config_path, "r");
    if (!src) return false;

    FILE *dst = fopen(backup_path, "w");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            unlink(backup_path);
            return false;
        }
    }

    fclose(src);
    fflush(dst);
    fsync(fileno(dst));
    fclose(dst);

    /* Copy to simple backup too */
    src = fopen(backup_path, "r");
    dst = fopen(simple_backup, "w");
    if (src && dst) {
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
            fwrite(buf, 1, n, dst);
        }
        fflush(dst);
        fsync(fileno(dst));
    }
    if (src) fclose(src);
    if (dst) fclose(dst);

    log_info("Config backup created: %s", backup_path);
    return true;
}

bool config_restore_from_backup(const char *config_path) {
    if (!config_path) return false;

    char backup_path[4096];
    snprintf(backup_path, sizeof(backup_path), "%s.backup", config_path);

    if (access(backup_path, F_OK) != 0) {
        log_error("No backup file found: %s", backup_path);
        return false;
    }

    /* Copy backup to main config */
    FILE *src = fopen(backup_path, "r");
    if (!src) return false;

    char temp_path[4096];
    snprintf(temp_path, sizeof(temp_path), "%s.restore", config_path);

    FILE *dst = fopen(temp_path, "w");
    if (!dst) {
        fclose(src);
        return false;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            unlink(temp_path);
            return false;
        }
    }

    fclose(src);
    fflush(dst);
    fsync(fileno(dst));
    fclose(dst);

    /* Atomic rename */
    if (rename(temp_path, config_path) != 0) {
        unlink(temp_path);
        return false;
    }

    fsync_path(config_path);

    log_info("Config restored from backup: %s", backup_path);
    return true;
}

/* ============================================================================
 * VIBE SERIALIZATION
 * ============================================================================ */

/* Build VIBE object from current state */
static VibeValue *build_vibe_from_state(struct neowall_state *state) {
    VibeValue *root = vibe_value_new_object();
    if (!root) return NULL;

    /* Iterate through all schema keys and serialize current values */
    const config_key_info_t *schema = config_get_schema();

    for (size_t i = 0; schema[i].key != NULL; i++) {
        const config_key_info_t *key = &schema[i];

        /* Skip output-specific keys (handled separately) */
        if (key->scope == CONFIG_SCOPE_OUTPUT) continue;

        /* Get section object (create if doesn't exist) */
        VibeValue *section_val = vibe_object_get(root->as_object, key->section);
        VibeObject *section;

        if (!section_val) {
            section_val = vibe_value_new_object();
            vibe_object_set(root->as_object, key->section, section_val);
        }
        section = section_val->as_object;

        /* Get current value (or default) */
        char value[1024];
        if (!config_get_value(state, key->key, value, sizeof(value))) {
            snprintf(value, sizeof(value), "%s", key->default_value);
        }

        /* Add to section based on type */
        VibeValue *val = NULL;
        switch (key->type) {
            case CONFIG_TYPE_INTEGER: {
                int64_t ival;
                if (config_parse_int(value, &ival)) {
                    val = vibe_value_new_integer(ival);
                }
                break;
            }
            case CONFIG_TYPE_FLOAT: {
                double fval;
                if (config_parse_float(value, &fval)) {
                    val = vibe_value_new_float(fval);
                }
                break;
            }
            case CONFIG_TYPE_BOOLEAN: {
                bool bval;
                if (config_parse_bool(value, &bval)) {
                    val = vibe_value_new_boolean(bval);
                }
                break;
            }
            case CONFIG_TYPE_STRING:
            case CONFIG_TYPE_ENUM:
            case CONFIG_TYPE_PATH:
                val = vibe_value_new_string(value);
                break;
        }

        if (val) {
            vibe_object_set(section, key->name, val);
        }
    }

    /* Add per-output sections in hierarchical VIBE format: outputs { DP-1 { ... } } */
    pthread_rwlock_rdlock(&state->output_list_lock);

    struct output_state *output = state->outputs;
    bool has_outputs = false;

    /* Check if we have any outputs to serialize */
    struct output_state *temp = output;
    while (temp) {
        if (temp->connector_name[0] != '\0') {
            has_outputs = true;
            break;
        }
        temp = temp->next;
    }

    if (has_outputs) {
        /* Create outputs container object */
        VibeValue *outputs_container = vibe_value_new_object();
        VibeObject *outputs_obj = outputs_container->as_object;

        while (output) {
            if (output->connector_name[0] == '\0') {
                output = output->next;
                continue;
            }

            /* Create object for this specific output (e.g., DP-1, HDMI-1) */
            VibeValue *output_obj = vibe_value_new_object();
            VibeObject *output_section_obj = output_obj->as_object;

            /* Add output-specific settings */
            if (output->config.duration > 0) {
                vibe_object_set(output_section_obj, "cycle_interval",
                              vibe_value_new_integer((int64_t)output->config.duration));
            }

            const char *mode_str = wallpaper_mode_to_string(output->config.mode);
            if (mode_str) {
                vibe_object_set(output_section_obj, "wallpaper_mode",
                              vibe_value_new_string(mode_str));
            }

            if (output->config.shader_speed != 1.0f) {
                vibe_object_set(output_section_obj, "shader_speed",
                              vibe_value_new_float(output->config.shader_speed));
            }

            if (output->config.shader_fps != 60) {
                vibe_object_set(output_section_obj, "fps_limit",
                              vibe_value_new_integer(output->config.shader_fps));
            }

            vibe_object_set(output_section_obj, "vsync",
                          vibe_value_new_boolean(output->config.vsync));

            /* Add this output to the outputs container */
            vibe_object_set(outputs_obj, output->connector_name, output_obj);

            output = output->next;
        }

        /* Add outputs container to root */
        vibe_object_set(root->as_object, "outputs", outputs_container);
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    return root;
}

/* Serialize VIBE object to string in proper hierarchical VIBE format */
static char *serialize_vibe(VibeValue *root) {
    if (!root) return NULL;

    /* Allocate buffer for serialization */
    size_t buf_size = MAX_CONFIG_SIZE;
    char *buffer = malloc(buf_size);
    if (!buffer) return NULL;

    size_t offset = 0;

    /* Add header comment */
    offset += snprintf(buffer + offset, buf_size - offset,
        "# NeoWall Configuration\n"
        "# Auto-generated - edit with 'neowall set-config' or manually\n"
        "# Format: VIBE (https://1ay1.github.io/vibe/)\n\n");

    /* Serialize with proper hierarchical VIBE format */
    VibeObject *root_obj = root->as_object;
    if (!root_obj) {
        free(buffer);
        return NULL;
    }

    /* Helper function to serialize nested objects recursively */
    void serialize_object(VibeObject *obj, int indent_level) {
        for (size_t i = 0; i < obj->count; i++) {
            const char *key = obj->entries[i].key;
            VibeValue *val = obj->entries[i].value;

            /* Create indentation */
            for (int ind = 0; ind < indent_level * 4; ind++) {
                offset += snprintf(buffer + offset, buf_size - offset, " ");
            }

            if (val->type == VIBE_TYPE_OBJECT) {
                /* Nested object: key { ... } */
                offset += snprintf(buffer + offset, buf_size - offset, "%s {\n", key);
                serialize_object(val->as_object, indent_level + 1);

                /* Close brace with proper indentation */
                for (int ind = 0; ind < indent_level * 4; ind++) {
                    offset += snprintf(buffer + offset, buf_size - offset, " ");
                }
                offset += snprintf(buffer + offset, buf_size - offset, "}\n");

                /* Add blank line after sections at root level */
                if (indent_level == 0) {
                    offset += snprintf(buffer + offset, buf_size - offset, "\n");
                }
            } else {
                /* Scalar value: key value */
                offset += snprintf(buffer + offset, buf_size - offset, "%s ", key);

                switch (val->type) {
                    case VIBE_TYPE_INTEGER:
                        offset += snprintf(buffer + offset, buf_size - offset,
                                         "%ld\n", val->as_integer);
                        break;
                    case VIBE_TYPE_FLOAT:
                        offset += snprintf(buffer + offset, buf_size - offset,
                                         "%.2f\n", val->as_float);
                        break;
                    case VIBE_TYPE_BOOLEAN:
                        offset += snprintf(buffer + offset, buf_size - offset,
                                         "%s\n", val->as_boolean ? "true" : "false");
                        break;
                    case VIBE_TYPE_STRING:
                        /* Quote strings if they contain spaces or special chars */
                        if (strchr(val->as_string, ' ') || strchr(val->as_string, '#') ||
                            strchr(val->as_string, '/')) {
                            offset += snprintf(buffer + offset, buf_size - offset,
                                             "\"%s\"\n", val->as_string);
                        } else {
                            offset += snprintf(buffer + offset, buf_size - offset,
                                             "%s\n", val->as_string);
                        }
                        break;
                    case VIBE_TYPE_NULL:
                        offset += snprintf(buffer + offset, buf_size - offset, "null\n");
                        break;
                    case VIBE_TYPE_ARRAY:
                        /* Arrays not yet supported in serialization */
                        offset += snprintf(buffer + offset, buf_size - offset, "[]\n");
                        break;
                    case VIBE_TYPE_OBJECT:
                        /* Already handled above */
                        break;
                }
            }
        }

        /* Close the section */
        offset += snprintf(buffer + offset, buf_size - offset, "}\n\n");
    }

    /* Serialize the root object */
    serialize_object(root_obj, 0);

    return buffer;
}

char *config_export_vibe(struct neowall_state *state) {
    if (!state) return NULL;

    VibeValue *root = build_vibe_from_state(state);
    if (!root) return NULL;

    char *result = serialize_vibe(root);
    vibe_value_free(root);

    return result;
}

/* ============================================================================
 * ATOMIC WRITE OPERATION
 * ============================================================================ */

bool config_write_to_disk(struct neowall_state *state, bool backup) {
    if (!state) return false;

    const char *config_path = state->config_path;
    if (!config_path || config_path[0] == '\0') {
        log_error("No config path set");
        return false;
    }

    /* Expand path */
    char expanded_path[4096];
    expand_path(config_path, expanded_path, sizeof(expanded_path));

    /* Create backup if requested */
    if (backup) {
        config_create_backup(expanded_path);
    }

    /* Serialize config to string */
    char *config_content = config_export_vibe(state);
    if (!config_content) {
        log_error("Failed to serialize configuration");
        return false;
    }

    /* Write to temporary file */
    char temp_path[4096];
    size_t path_len = strlen(expanded_path);
    if (path_len > sizeof(temp_path) - 5) {
        log_error("Config path too long");
        free(config_content);
        return false;
    }
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", expanded_path);

    FILE *fp = fopen(temp_path, "w");
    if (!fp) {
        log_error("Failed to create temp config file: %s", strerror(errno));
        free(config_content);
        return false;
    }

    size_t len = strlen(config_content);
    if (fwrite(config_content, 1, len, fp) != len) {
        log_error("Failed to write config: %s", strerror(errno));
        fclose(fp);
        unlink(temp_path);
        free(config_content);
        return false;
    }

    /* Ensure data is written to disk */
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    free(config_content);

    /* Atomic rename */
    if (rename(temp_path, expanded_path) != 0) {
        log_error("Failed to rename config file: %s", strerror(errno));
        unlink(temp_path);
        return false;
    }

    /* Fsync directory to ensure directory entry is persisted */
    fsync_path(expanded_path);

    log_info("Configuration written to %s", expanded_path);
    return true;
}

/* ============================================================================
 * SET VALUE API
 * ============================================================================ */

bool config_set_value(struct neowall_state *state,
                      const char *key,
                      const char *value,
                      char *error_buf,
                      size_t error_len) {
    if (!state || !key || !value) {
        set_error(error_buf, error_len, "Invalid arguments");
        return false;
    }

    /* Validate value using schema */
    config_validation_result_t result = config_validate_value(key, value);
    if (!result.valid) {
        set_error(error_buf, error_len, "%s", result.error_message);
        return false;
    }

    /* Validate using configuration rules (type-specific validation) */
    char rules_error[512];
    if (!config_validate_rules(state, NULL, key, value, rules_error, sizeof(rules_error))) {
        set_error(error_buf, error_len, "%s", rules_error);
        return false;
    }

    /* Look up key info */
    const config_key_info_t *info = config_lookup_key(key);
    if (!info) {
        set_error(error_buf, error_len, "Unknown config key: %s", key);
        return false;
    }

    /* Apply to runtime state */
    if (info->apply) {
        if (!info->apply(state, value)) {
            set_error(error_buf, error_len, "Failed to apply config change");
            return false;
        }
    }

    /* Write to disk atomically */
    if (!config_write_to_disk(state, true)) {
        set_error(error_buf, error_len, "Failed to write config to disk");
        return false;
    }

    log_info("Config updated: %s = %s", key, value);
    return true;
}

bool config_set_output_value(struct neowall_state *state,
                             const char *output_name,
                             const char *key,
                             const char *value,
                             char *error_buf,
                             size_t error_len) {
    if (!state || !output_name || !key || !value) {
        set_error(error_buf, error_len, "Invalid arguments");
        return false;
    }

    /* Build full key path */
    char full_key[512];
    snprintf(full_key, sizeof(full_key), "outputs.%s.%s", output_name, key);

    /* Validate against base key (without output prefix) */
    config_validation_result_t result = config_validate_value(key, value);
    if (!result.valid) {
        set_error(error_buf, error_len, "%s", result.error_message);
        return false;
    }

    /* Validate using configuration rules for this specific output */
    char rules_error[512];
    if (!config_validate_rules(state, output_name, key, value, rules_error, sizeof(rules_error))) {
        set_error(error_buf, error_len, "%s", rules_error);
        return false;
    }

    /* Find output */
    /* Find output and get value */
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
        set_error(error_buf, error_len, "Output not found: %s", output_name);
        return false;
    }

    /* Look up key info */
    const config_key_info_t *info = config_lookup_key(key);
    if (!info) {
        pthread_rwlock_unlock(&state->output_list_lock);
        set_error(error_buf, error_len, "Unknown config key: %s", key);
        return false;
    }

    /* Apply to output */
    bool success = true;
    if (info->apply_output) {
        success = info->apply_output(output, value);
    }

    pthread_rwlock_unlock(&state->output_list_lock);

    if (!success) {
        set_error(error_buf, error_len, "Failed to apply config to output");
        return false;
    }

    /* Write to disk atomically */
    if (!config_write_to_disk(state, true)) {
        set_error(error_buf, error_len, "Failed to write config to disk");
        return false;
    }

    log_info("Output config updated: %s.%s = %s", output_name, key, value);
    return true;
}

/* ============================================================================
 * GET VALUE API
 * ============================================================================ */

bool config_get_value(struct neowall_state *state,
                     const char *key,
                     char *value_buf,
                     size_t value_len) {
    if (!state || !key || !value_buf || value_len == 0) {
        return false;
    }

    /* For now, return default value */
    /* TODO: Store actual values in state structure */
    const char *default_val = config_get_default(key);
    if (!default_val) {
        return false;
    }

    snprintf(value_buf, value_len, "%s", default_val);
    return true;
}

bool config_get_output_value(struct neowall_state *state,
                            const char *output_name,
                            const char *key,
                            char *value_buf,
                            size_t value_len) {
    if (!state || !output_name || !key || !value_buf || value_len == 0) {
        return false;
    }

    /* Find output and get value */
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
        return false;
    }

    /* Extract value based on key */
    bool found = true;
    if (strcmp(key, "cycle_interval") == 0) {
        snprintf(value_buf, value_len, "%d", (int)output->config.duration);
    } else if (strcmp(key, "wallpaper_mode") == 0) {
        const char *mode = wallpaper_mode_to_string(output->config.mode);
        snprintf(value_buf, value_len, "%s", mode ? mode : "fill");
    } else if (strcmp(key, "shader_speed") == 0) {
        snprintf(value_buf, value_len, "%.2f", output->config.shader_speed);
    } else {
        found = false;
    }

    pthread_rwlock_unlock(&state->output_list_lock);
    return found;
}

/* ============================================================================
 * APPLY CHANGES TO RUNTIME
 * ============================================================================ */

bool config_apply_change(struct neowall_state *state,
                        const char *key,
                        const char *value) {
    if (!state || !key || !value) return false;

    const config_key_info_t *info = config_lookup_key(key);
    if (!info || !info->apply) return true; /* No apply function is OK */

    return info->apply(state, value);
}

bool config_apply_output_change(struct output_state *output,
                               const char *key,
                               const char *value) {
    if (!output || !key || !value) return false;

    const config_key_info_t *info = config_lookup_key(key);
    if (!info || !info->apply_output) return true;

    return info->apply_output(output, value);
}
