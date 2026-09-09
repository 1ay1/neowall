/* Persistent per-shader state. See include/neowall/shader/shader_state.h. */

#define _POSIX_C_SOURCE 200809L

#include "neowall/shader/shader_state.h"
#include "neowall/neowall.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Directory holding one file per shader. */
static bool state_dir(char *out, size_t out_size) {
    const char *base = getenv("XDG_STATE_HOME");
    char root[MAX_PATH_LENGTH];

    if (base && base[0] == '/') {
        snprintf(root, sizeof(root), "%s/neowall", base);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] != '/') return false;
        snprintf(root, sizeof(root), "%s/.local/state/neowall", home);
    }

    /* mkdir the chain; EEXIST is success. */
    if (mkdir(root, 0700) != 0 && errno != EEXIST) return false;

    int n = snprintf(out, out_size, "%s/shader-state", root);
    if (n < 0 || (size_t)n >= out_size) return false;
    if (mkdir(out, 0700) != 0 && errno != EEXIST) return false;

    return true;
}

/* Map a shader path to a flat filename: '/' becomes '%' so the whole path
 * stays visible (and unique) in one directory entry. */
static void state_key(const char *shader_path, char *out, size_t out_size) {
    size_t o = 0;
    for (const char *p = shader_path; *p && o + 1 < out_size; p++) {
        char c = *p;
        if (c == '/' || c == '\\') c = '%';
        out[o++] = c;
    }
    out[o] = '\0';
}

static bool state_file_path(const char *shader_path, char *out, size_t out_size) {
    char dir[MAX_PATH_LENGTH];
    if (!state_dir(dir, sizeof(dir))) return false;

    char key[512];
    state_key(shader_path, key, sizeof(key));

    int n = snprintf(out, out_size, "%s/%s.state", dir, key);
    return n > 0 && (size_t)n < out_size;
}

void nw_shader_state_load(const char *shader_path, nw_shader_state_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!shader_path || !*shader_path) return;

    char path[MAX_PATH_LENGTH];
    if (!state_file_path(shader_path, path, sizeof(path))) return;

    FILE *fp = fopen(path, "r");
    if (!fp) return;   /* first run */

    long long saved_at = 0;
    if (fscanf(fp, "%lld", &saved_at) != 1) {
        fclose(fp);
        return;
    }

    /* A short or malformed file leaves the remaining slots at zero rather than
     * failing: a shader that gained a slot since the last save still starts. */
    for (int i = 0; i < NW_STATE_FLOATS; i++) {
        double v = 0.0;
        if (fscanf(fp, "%lf", &v) != 1) break;
        out->values[i] = (float)v;
    }
    fclose(fp);

    out->saved_at = (int64_t)saved_at;
    log_debug("shader-state: loaded %s (age %.0fs)", path, (double)nw_shader_state_age(out));
}

bool nw_shader_state_save(const char *shader_path, const nw_shader_state_t *state) {
    if (!shader_path || !*shader_path || !state) return false;

    char path[MAX_PATH_LENGTH];
    if (!state_file_path(shader_path, path, sizeof(path))) return false;

    /* Write-then-rename: a crash mid-write leaves the previous good state in
     * place instead of a truncated file that would read back as garbage. */
    char tmp[MAX_PATH_LENGTH];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return false;

    FILE *fp = fopen(tmp, "w");
    if (!fp) {
        log_debug("shader-state: cannot write %s: %s", tmp, strerror(errno));
        return false;
    }

    fprintf(fp, "%lld\n", (long long)time(NULL));
    for (int i = 0; i < NW_STATE_FLOATS; i++) {
        fprintf(fp, "%.9g%c", (double)state->values[i],
                (i % 4 == 3) ? '\n' : ' ');
    }

    /* Flush before rename so the rename publishes complete data. */
    if (fflush(fp) != 0 || fclose(fp) != 0) {
        unlink(tmp);
        return false;
    }

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return false;
    }

    return true;
}

float nw_shader_state_age(const nw_shader_state_t *state) {
    if (!state || state->saved_at <= 0) return 0.0f;

    int64_t now = (int64_t)time(NULL);
    int64_t age = now - state->saved_at;
    if (age < 0) age = 0;      /* clock stepped back */
    return (float)age;
}
