/* Small helpers: index parsing, ~ expansion, data dirs, private dirs.
 * See neowall/core/util.h. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE   /* lstat, localtime_r, PATH_MAX, clock_gettime */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>  /* PRIu64 */
#include <limits.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>  /* POSIX strcasecmp — don't redefine it ourselves */
#include <unistd.h>
#include <errno.h>

#include "neowall/core/util.h"

#include "neowall/core/constants.h"
#include "neowall/core/log.h"
#include "version.h"   /* NEOWALL_DATADIR */

#define MAX_PATH_LENGTH NW_PATH_MAX

/* Parse a non-negative decimal index out of `s`.
 *
 * atoi(3) has no way to report failure, and C11 7.22.1p1 makes it undefined
 * behaviour when the value cannot be represented as an int - so a digit string
 * that overflows int cannot be handled safely by the caller. Values that come
 * from the command line or from the on-disk state file go through this instead.
 *
 * Accepts only a non-empty run of decimal digits: no sign, no leading or
 * trailing whitespace, no trailing garbage. Overflow (ERANGE) is rejected
 * rather than clamped. On success stores the value in *out and returns true;
 * *out is untouched on failure. */
bool neowall_parse_index(const char *s, long *out) {
    if (!s || !out || *s == '\0') {
        return false;
    }

    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }

    errno = 0;
    char *end = NULL;
    long value = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || value < 0) {
        return false;
    }

    *out = value;
    return true;
}

/* Path expansion (tilde expansion) */
bool expand_path(const char *path, char *expanded, size_t size) {
    if (!path || !expanded || size == 0) {
        return false;
    }

    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~: HOME not set");
            return false;
        }

        size_t home_len = strlen(home);
        size_t path_len = strlen(path + 1);

        if (home_len + path_len + 1 > size) {
            log_error("Expanded path too long");
            return false;
        }

        snprintf(expanded, size, "%s%s", home, path + 1);
        return true;
    }

    /* No expansion needed */
    if (strlen(path) >= size) {
        log_error("Path too long");
        return false;
    }

    strncpy(expanded, path, size - 1);
    expanded[size - 1] = '\0';
    return true;
}

bool neowall_each_data_dir(bool (*fn)(const char *dir, void *ctx), void *ctx) {
    char dir[PATH_MAX];
    int n;

    const char *dh = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (dh && dh[0] == '/') {
        n = snprintf(dir, sizeof(dir), "%s/neowall", dh);
    } else if (home && home[0]) {
        n = snprintf(dir, sizeof(dir), "%s/.local/share/neowall", home);
    } else {
        n = -1;
    }
    if (n > 0 && (size_t)n < sizeof(dir) && access(dir, F_OK) == 0 && fn(dir, ctx)) return true;

    /* XDG says relative entries are invalid, and an unset/empty value means
     * /usr/local/share:/usr/share. */
    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !dirs[0]) dirs = "/usr/local/share:/usr/share";
    bool seen_builtin = false;
    for (const char *p = dirs; *p;) {
        const char *end = strchr(p, ':');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > 0 && p[0] == '/') {
            n = snprintf(dir, sizeof(dir), "%.*s/neowall", (int)len, p);
            if (n > 0 && (size_t)n < sizeof(dir)) {
                if (strcmp(dir, NEOWALL_DATADIR) == 0) seen_builtin = true;
                if (access(dir, F_OK) == 0 && fn(dir, ctx)) return true;
            }
        }
        p += len;
        if (*p == ':') p++;
    }

    if (!seen_builtin && access(NEOWALL_DATADIR, F_OK) == 0 && fn(NEOWALL_DATADIR, ctx)) {
        return true;
    }
    return false;
}

struct find_data_ctx {
    const char *rel;
    char *out;
    size_t size;
};

static bool find_data_in(const char *dir, void *vctx) {
    struct find_data_ctx *c = vctx;
    int n = snprintf(c->out, c->size, "%s/%s", dir, c->rel);
    return n > 0 && (size_t)n < c->size && access(c->out, F_OK) == 0;
}

bool neowall_find_data(const char *rel, char *out, size_t size) {
    if (!rel || !out || size == 0) return false;
    struct find_data_ctx c = {rel, out, size};
    if (neowall_each_data_dir(find_data_in, &c)) return true;
    out[0] = '\0';
    return false;
}

void config_str_set(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (dst == src) return;   /* already holds the value */
    snprintf(dst, dst_size, "%s", src);
}

/* Ease in-out cubic function for smooth transitions */
float ease_in_out_cubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = (2.0f * t) - 2.0f;
        return 0.5f * f * f * f + 1.0f;
    }
}

/* Create `dir` 0700 if absent, then verify it is a directory we own with no
 * group/other access and is not a symlink. Returns true only when the path is
 * safe to drop predictable-named files into. lstat (not stat) is deliberate so
 * a symlink planted by another user is rejected rather than followed. */
static bool ensure_private_dir(const char *dir) {
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat st;
    if (lstat(dir, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return false;      /* symlink or regular file squatting the name */
    }
    if (st.st_uid != getuid()) {
        return false;      /* someone else owns it */
    }
    if (st.st_mode & (S_IRWXG | S_IRWXO)) {
        /* Tighten perms if a previous umask left it group/other-accessible. */
        if (chmod(dir, 0700) != 0) {
            return false;
        }
    }
    return true;
}

const char *neowall_secure_runtime_dir(void) {
    static char dir[MAX_PATH_LENGTH];

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && runtime_dir[0] != '\0') {
        /* $XDG_RUNTIME_DIR is already a per-user 0700 tmpfs; a neowall/
         * subdir under it inherits that isolation. */
        snprintf(dir, sizeof(dir), "%s/neowall", runtime_dir);
        if (ensure_private_dir(dir)) {
            return dir;
        }
    }

    /* Fallback: /tmp is world-writable, so the per-uid subdir MUST be created
     * and ownership-checked rather than blindly written into. */
    snprintf(dir, sizeof(dir), "/tmp/neowall-%d", (int)getuid());
    if (ensure_private_dir(dir)) {
        return dir;
    }

    return NULL;
}
