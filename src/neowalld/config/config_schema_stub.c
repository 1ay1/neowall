/* Temporary stub implementations for config_schema functions */
#include "config_schema.h"
#include <stdlib.h>
#include <string.h>

char *config_export_schema_json(void) {
    return strdup("{\"status\":\"schema export not yet implemented\"}");
}

const char *config_get_default(const char *key) {
    (void)key;
    return "";
}

bool config_reset_to_default(struct neowall_state *state, const char *key) {
    (void)state;
    (void)key;
    return true;
}

bool config_parse_int(const char *str, int64_t *out) {
    if (!str || !out) return false;
    *out = atoll(str);
    return true;
}

bool config_parse_float(const char *str, double *out) {
    if (!str || !out) return false;
    *out = atof(str);
    return true;
}

bool config_parse_bool(const char *str, bool *out) {
    if (!str || !out) return false;
    if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0 || strcmp(str, "yes") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(str, "false") == 0 || strcmp(str, "0") == 0 || strcmp(str, "no") == 0) {
        *out = false;
        return true;
    }
    return false;
}

const config_key_info_t *config_get_schema(void) {
    return NULL;
}

size_t config_get_schema_count(void) {
    return 0;
}

const config_key_info_t *config_lookup_key(const char *key) {
    (void)key;
    return NULL;
}

config_validation_result_t config_validate_value(const char *key, const char *value) {
    (void)key;
    (void)value;
    config_validation_result_t result = {.valid = true};
    result.error_message[0] = '\0';
    return result;
}
