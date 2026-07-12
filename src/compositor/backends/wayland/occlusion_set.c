#include "occlusion_set.h"

bool output_set_add(output_set *set, void *output) {
    if (!set || !output) {
        return false;
    }
    if (output_set_contains(set, output)) {
        return true;
    }
    return output_set_push(set, output);
}

void output_set_remove(output_set *set, void *output) {
    if (!set || !output) {
        return;
    }
    for (size_t i = 0; i < set->len; i++) {
        if (set->data[i] == output) {
            set->data[i] = set->data[set->len - 1];
            set->len--;
            return;
        }
    }
}

bool output_set_contains(const output_set *set, const void *output) {
    if (!set || !output) {
        return false;
    }
    for (size_t i = 0; i < set->len; i++) {
        if (set->data[i] == output) {
            return true;
        }
    }
    return false;
}
