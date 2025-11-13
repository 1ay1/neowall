#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "vibe_path.h"
#include "vibe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

/* Maximum path depth */
#define MAX_PATH_DEPTH 16
#define MAX_PATH_COMPONENT 256

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Split path into components
 * Returns number of components, fills components array
 */
static size_t split_path(const char *path, char components[][MAX_PATH_COMPONENT], size_t max_components) {
    if (!path || !components || max_components == 0) return 0;

    size_t count = 0;
    const char *start = path;
    const char *dot = strchr(start, '.');

    while (dot && count < max_components) {
        size_t len = dot - start;
        if (len >= MAX_PATH_COMPONENT) return 0; /* Component too long */
        if (len == 0) return 0; /* Empty component */

        memcpy(components[count], start, len);
        components[count][len] = '\0';
        count++;

        start = dot + 1;
        dot = strchr(start, '.');
    }

    /* Last component */
    size_t len = strlen(start);
    if (len > 0 && len < MAX_PATH_COMPONENT && count < max_components) {
        strcpy(components[count], start);
        count++;
    }

    return count;
}

/**
 * Navigate to a specific path in the VIBE tree
 * Returns the value at path, or NULL if not found
 */
static VibeValue *navigate_to_path(VibeValue *root, const char *path, bool create_if_missing) {
    if (!root || !path || root->type != VIBE_TYPE_OBJECT) return NULL;

    char components[MAX_PATH_DEPTH][MAX_PATH_COMPONENT];
    size_t count = split_path(path, components, MAX_PATH_DEPTH);

    if (count == 0) return NULL;

    VibeValue *current = root;

    for (size_t i = 0; i < count; i++) {
        if (current->type != VIBE_TYPE_OBJECT) return NULL;

        VibeValue *next = vibe_object_get(current->as_object, components[i]);

        if (!next) {
            if (!create_if_missing) return NULL;

            /* Create intermediate object */
            next = vibe_value_new_object();
            if (!next) return NULL;

            vibe_object_set(current->as_object, components[i], next);
        }

        current = next;
    }

    return current;
}

/**
 * Navigate to parent and get key name
 * Creates intermediate objects if they don't exist
 */
static VibeValue *navigate_to_parent(VibeValue *root, const char *path,
                                     char *key_out, size_t key_size) {
    if (!root || !path || !key_out || key_size == 0) return NULL;

    char components[MAX_PATH_DEPTH][MAX_PATH_COMPONENT];
    size_t count = split_path(path, components, MAX_PATH_DEPTH);

    if (count == 0) return NULL;
    if (count == 1) {
        /* Top level - root is parent */
        strncpy(key_out, components[0], key_size - 1);
        key_out[key_size - 1] = '\0';
        return root;
    }

    /* Navigate to parent, creating intermediate objects as needed */
    VibeValue *current = root;
    for (size_t i = 0; i < count - 1; i++) {
        if (current->type != VIBE_TYPE_OBJECT) return NULL;

        VibeValue *next = vibe_object_get(current->as_object, components[i]);
        if (!next) {
            /* Create intermediate object */
            next = vibe_value_new_object();
            if (!next) return NULL;
            vibe_object_set(current->as_object, components[i], next);
        }

        if (next->type != VIBE_TYPE_OBJECT) return NULL;

        current = next;
    }

    /* Return key name */
    strncpy(key_out, components[count - 1], key_size - 1);
    key_out[key_size - 1] = '\0';

    return current;
}

/* ============================================================================
 * Path Get Operations
 * ============================================================================ */

VibeValue *vibe_path_get(VibeValue *root, const char *path) {
    return navigate_to_path(root, path, false);
}

const char *vibe_path_get_string(VibeValue *root, const char *path) {
    VibeValue *val = vibe_path_get(root, path);
    if (!val || val->type != VIBE_TYPE_STRING) return NULL;
    return val->as_string;
}

bool vibe_path_get_int(VibeValue *root, const char *path, int64_t *out) {
    if (!out) return false;

    VibeValue *val = vibe_path_get(root, path);
    if (!val || val->type != VIBE_TYPE_INTEGER) return false;

    *out = val->as_integer;
    return true;
}

bool vibe_path_get_float(VibeValue *root, const char *path, double *out) {
    if (!out) return false;

    VibeValue *val = vibe_path_get(root, path);
    if (!val) return false;

    if (val->type == VIBE_TYPE_FLOAT) {
        *out = val->as_float;
        return true;
    } else if (val->type == VIBE_TYPE_INTEGER) {
        *out = (double)val->as_integer;
        return true;
    }

    return false;
}

bool vibe_path_get_bool(VibeValue *root, const char *path, bool *out) {
    if (!out) return false;

    VibeValue *val = vibe_path_get(root, path);
    if (!val || val->type != VIBE_TYPE_BOOLEAN) return false;

    *out = val->as_boolean;
    return true;
}

bool vibe_path_exists(VibeValue *root, const char *path) {
    return vibe_path_get(root, path) != NULL;
}

/* ============================================================================
 * Path Set Operations
 * ============================================================================ */

bool vibe_path_set(VibeValue *root, const char *path, VibeValue *value) {
    if (!root || !path || !value) return false;

    char key[MAX_PATH_COMPONENT];
    VibeValue *parent = navigate_to_parent(root, path, key, sizeof(key));

    if (!parent || parent->type != VIBE_TYPE_OBJECT) return false;

    vibe_object_set(parent->as_object, key, value);
    return true;
}

bool vibe_path_set_string(VibeValue *root, const char *path, const char *value) {
    if (!value) return false;

    VibeValue *val = vibe_value_new_string(value);
    if (!val) return false;

    return vibe_path_set(root, path, val);
}

bool vibe_path_set_int(VibeValue *root, const char *path, int64_t value) {
    VibeValue *val = vibe_value_new_integer(value);
    if (!val) return false;

    return vibe_path_set(root, path, val);
}

bool vibe_path_set_float(VibeValue *root, const char *path, double value) {
    VibeValue *val = vibe_value_new_float(value);
    if (!val) return false;

    return vibe_path_set(root, path, val);
}

bool vibe_path_set_bool(VibeValue *root, const char *path, bool value) {
    VibeValue *val = vibe_value_new_boolean(value);
    if (!val) return false;

    return vibe_path_set(root, path, val);
}

/* ============================================================================
 * Path Delete Operations
 * ============================================================================ */

bool vibe_path_delete(VibeValue *root, const char *path) {
    if (!root || !path) return false;

    char key[MAX_PATH_COMPONENT];
    VibeValue *parent = navigate_to_parent(root, path, key, sizeof(key));

    if (!parent || parent->type != VIBE_TYPE_OBJECT) return false;

    /* Find and remove the key */
    VibeObject *obj = parent->as_object;
    for (size_t i = 0; i < obj->count; i++) {
        if (strcmp(obj->entries[i].key, key) == 0) {
            /* Free the value */
            vibe_value_free(obj->entries[i].value);
            free(obj->entries[i].key);

            /* Shift remaining entries */
            for (size_t j = i; j < obj->count - 1; j++) {
                obj->entries[j] = obj->entries[j + 1];
            }

            obj->count--;
            return true;
        }
    }

    return false;
}

/* ============================================================================
 * File I/O Operations
 * ============================================================================ */

/**
 * Serialize VIBE value to string with formatting
 */
static void serialize_vibe_value(VibeValue *value, FILE *fp, int indent, int indent_size) {
    if (!value || !fp) return;

    const char *spaces = "                                                    ";
    int indent_len = indent * indent_size;
    if (indent_len > 50) indent_len = 50;

    switch (value->type) {
        case VIBE_TYPE_NULL:
            fprintf(fp, "null");
            break;

        case VIBE_TYPE_BOOLEAN:
            fprintf(fp, "%s", value->as_boolean ? "true" : "false");
            break;

        case VIBE_TYPE_INTEGER:
            fprintf(fp, "%ld", value->as_integer);
            break;

        case VIBE_TYPE_FLOAT:
            fprintf(fp, "%.2f", value->as_float);
            break;

        case VIBE_TYPE_STRING:
            /* Check if string needs quotes (has spaces or special chars) */
            if (strchr(value->as_string, ' ') ||
                strchr(value->as_string, '\t') ||
                strchr(value->as_string, '\n')) {
                fprintf(fp, "\"%s\"", value->as_string);
            } else {
                fprintf(fp, "%s", value->as_string);
            }
            break;

        case VIBE_TYPE_ARRAY:
            fprintf(fp, "[\n");
            for (size_t i = 0; i < value->as_array->count; i++) {
                fprintf(fp, "%.*s", indent_len + indent_size, spaces);
                serialize_vibe_value(value->as_array->values[i], fp, indent + 1, indent_size);
                if (i < value->as_array->count - 1) fprintf(fp, ",");
                fprintf(fp, "\n");
            }
            fprintf(fp, "%.*s]", indent_len, spaces);
            break;

        case VIBE_TYPE_OBJECT:
            fprintf(fp, "{\n");
            for (size_t i = 0; i < value->as_object->count; i++) {
                fprintf(fp, "%.*s%s ",
                       indent_len + indent_size, spaces,
                       value->as_object->entries[i].key);
                serialize_vibe_value(value->as_object->entries[i].value,
                                   fp, indent + 1, indent_size);
                fprintf(fp, "\n");
            }
            fprintf(fp, "%.*s}", indent_len, spaces);
            break;
    }
}

bool vibe_path_write_file_formatted(VibeValue *root, const char *path, int indent_size) {
    if (!root || !path) return false;

    /* Expand ~ to home directory */
    char expanded_path[4096];
    if (path[0] == '~' && path[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) return false;
        snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, path + 1);
    } else {
        strncpy(expanded_path, path, sizeof(expanded_path) - 1);
        expanded_path[sizeof(expanded_path) - 1] = '\0';
    }

    /* Write to temp file */
    char tmp_path[4100];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", expanded_path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open temp file for writing: %s\n", tmp_path);
        return false;
    }

    /* Write header comment */
    fprintf(fp, "# NeoWall Configuration\n");
    fprintf(fp, "# Auto-generated by neowall\n");
    fprintf(fp, "# Format: VIBE (https://1ay1.github.io/vibe/)\n\n");

    /* Serialize root object */
    if (root->type == VIBE_TYPE_OBJECT) {
        for (size_t i = 0; i < root->as_object->count; i++) {
            fprintf(fp, "%s ", root->as_object->entries[i].key);
            serialize_vibe_value(root->as_object->entries[i].value, fp, 0, indent_size);
            fprintf(fp, "\n\n");
        }
    }

    /* Ensure data is written */
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    /* Atomic rename */
    if (rename(tmp_path, expanded_path) != 0) {
        fprintf(stderr, "Failed to rename temp file: %s\n", strerror(errno));
        unlink(tmp_path);
        return false;
    }

    return true;
}

bool vibe_path_write_file(VibeValue *root, const char *path) {
    return vibe_path_write_file_formatted(root, path, 4);
}

/* ============================================================================
 * Utility Operations
 * ============================================================================ */

size_t vibe_path_list_keys(VibeValue *root, const char *path,
                           const char **keys, size_t max_keys) {
    if (!root || !keys || max_keys == 0) return 0;

    VibeValue *node;
    if (path && strlen(path) > 0) {
        node = vibe_path_get(root, path);
    } else {
        node = root;
    }

    if (!node || node->type != VIBE_TYPE_OBJECT) return 0;

    size_t count = 0;
    for (size_t i = 0; i < node->as_object->count && count < max_keys; i++) {
        keys[count++] = node->as_object->entries[i].key;
    }

    return count;
}

/**
 * Recursive helper for listing all keys
 */
static void list_all_keys_recursive(VibeValue *value, const char *prefix,
                                    const char **keys, size_t *count, size_t max_keys) {
    if (!value || *count >= max_keys) return;

    if (value->type != VIBE_TYPE_OBJECT) {
        /* Leaf node - add to list */
        if (*count < max_keys) {
            keys[*count] = strdup(prefix);
            (*count)++;
        }
        return;
    }

    /* Recurse into object */
    for (size_t i = 0; i < value->as_object->count && *count < max_keys; i++) {
        char new_prefix[4096];
        if (strlen(prefix) > 0) {
            snprintf(new_prefix, sizeof(new_prefix), "%s.%s",
                    prefix, value->as_object->entries[i].key);
        } else {
            snprintf(new_prefix, sizeof(new_prefix), "%s",
                    value->as_object->entries[i].key);
        }

        list_all_keys_recursive(value->as_object->entries[i].value,
                               new_prefix, keys, count, max_keys);
    }
}

size_t vibe_path_list_all_keys(VibeValue *root, const char **keys, size_t max_keys) {
    if (!root || !keys || max_keys == 0) return 0;

    size_t count = 0;
    list_all_keys_recursive(root, "", keys, &count, max_keys);
    return count;
}

bool vibe_path_validate(const char *path) {
    if (!path || strlen(path) == 0) return false;

    /* Check for leading/trailing dots */
    if (path[0] == '.' || path[strlen(path) - 1] == '.') return false;

    /* Check for double dots */
    if (strstr(path, "..")) return false;

    /* Check for valid characters */
    for (const char *p = path; *p; p++) {
        if (!isalnum(*p) && *p != '.' && *p != '-' && *p != '_') {
            return false;
        }
    }

    return true;
}

bool vibe_path_split(const char *path,
                     char *parent_buf, size_t parent_size,
                     char *key_buf, size_t key_size) {
    if (!path || !parent_buf || !key_buf) return false;
    if (parent_size == 0 || key_size == 0) return false;

    const char *last_dot = strrchr(path, '.');

    if (!last_dot) {
        /* No parent - root level */
        parent_buf[0] = '\0';
        strncpy(key_buf, path, key_size - 1);
        key_buf[key_size - 1] = '\0';
        return true;
    }

    /* Copy parent */
    size_t parent_len = last_dot - path;
    if (parent_len >= parent_size) return false;
    memcpy(parent_buf, path, parent_len);
    parent_buf[parent_len] = '\0';

    /* Copy key */
    strncpy(key_buf, last_dot + 1, key_size - 1);
    key_buf[key_size - 1] = '\0';

    return true;
}
