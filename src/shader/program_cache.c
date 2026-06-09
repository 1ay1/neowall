/* GLSL program binary cache.
 *
 * Driver shader compilation dominates shader-switch latency (50-300ms for a
 * complex raymarcher). GL_ARB_get_program_binary lets us snapshot the
 * driver-compiled program to disk and reload it next time in ~1ms.
 *
 * Cache key: FNV-1a hash of the COMPLETE wrapped fragment source (which embeds
 * the wrapper version implicitly) + driver identity (GL_VENDOR/RENDERER/
 * VERSION). A driver update changes the identity string, invalidating stale
 * binaries naturally; glProgramBinary additionally rejects incompatible blobs
 * at load (we fall back to source compilation), so the worst case is a miss.
 *
 * Layout: $XDG_CACHE_HOME/neowall/shaderbin/<hash16>.bin
 *   header: u32 magic, u32 version, u32 gl_format, u32 length
 *   payload: driver binary blob
 *
 * All functions are safe to call without the extension: they degrade to
 * "always miss / never store".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#include "neowall/shader/platform_compat.h"
#include "neowall/shader/program_cache.h"
#include "neowall/shader/shader_log.h"

#define CACHE_MAGIC   0x4E574243u  /* "NWBC" */
#define CACHE_VERSION 1u

static bool g_checked = false;
static bool g_supported = false;
static char g_dir[512];
static uint64_t g_driver_hash = 0;

/* FNV-1a 64-bit */
static uint64_t fnv1a(const void *data, size_t len, uint64_t seed) {
    const unsigned char *p = data;
    uint64_t h = seed ? seed : 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

/* One-time probe: extension support, cache dir, driver identity hash.
 * Must be called with a current GL context. */
static void cache_init_once(void) {
    if (g_checked) return;
    g_checked = true;

    /* GL 3.3 core: GL_ARB_get_program_binary is an extension (core in 4.1).
     * Probe via GL_NUM_PROGRAM_BINARY_FORMATS: zero formats = can't cache. */
    GLint num_formats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &num_formats);
    /* Clear any error from drivers that don't know the pname at all */
    while (glGetError() != GL_NO_ERROR) { /* drain */ }
    if (num_formats <= 0) {
        log_info("Program binary cache: unsupported by driver (0 binary formats)");
        return;
    }

    /* Driver identity for the key: a mesa/NVIDIA update invalidates blobs */
    const char *vendor   = (const char *)glGetString(GL_VENDOR);
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version  = (const char *)glGetString(GL_VERSION);
    uint64_t h = fnv1a(vendor   ? vendor   : "", vendor   ? strlen(vendor)   : 0, 0);
    h = fnv1a(renderer ? renderer : "", renderer ? strlen(renderer) : 0, h);
    h = fnv1a(version  ? version  : "", version  ? strlen(version)  : 0, h);
    g_driver_hash = h;

    /* Resolve cache dir: $XDG_CACHE_HOME/neowall/shaderbin or ~/.cache/... */
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) {
        snprintf(g_dir, sizeof(g_dir), "%s/neowall/shaderbin", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) {
            log_info("Program binary cache: no HOME, disabled");
            return;
        }
        snprintf(g_dir, sizeof(g_dir), "%s/.cache/neowall/shaderbin", home);
    }

    /* mkdir -p (two levels) */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", g_dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        log_info("Program binary cache: cannot create %s (%s), disabled",
                 g_dir, strerror(errno));
        return;
    }

    g_supported = true;
    log_info("Program binary cache enabled: %s (%d formats)", g_dir, num_formats);
}

static void cache_path_for(uint64_t key, char *out, size_t out_len) {
    snprintf(out, out_len, "%s/%016llx.bin", g_dir, (unsigned long long)key);
}

uint64_t program_cache_key(const char *vertex_src, const char *fragment_src) {
    uint64_t h = fnv1a(vertex_src   ? vertex_src   : "",
                       vertex_src   ? strlen(vertex_src)   : 0, 0);
    h = fnv1a(fragment_src ? fragment_src : "",
              fragment_src ? strlen(fragment_src) : 0, h);
    /* fold in driver identity (g_driver_hash is 0 until init; key consumers
     * call load/store which init first, so fold there instead) */
    return h;
}

bool program_cache_load(uint64_t key, GLuint *out_program) {
    cache_init_once();
    if (!g_supported || !out_program) return false;

    char path[600];
    cache_path_for(key ^ g_driver_hash, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint32_t hdr[4];
    bool ok = false;
    void *blob = NULL;

    if (fread(hdr, sizeof(hdr), 1, f) == 1 &&
        hdr[0] == CACHE_MAGIC && hdr[1] == CACHE_VERSION &&
        hdr[3] > 0 && hdr[3] < (64u << 20)) {
        blob = malloc(hdr[3]);
        if (blob && fread(blob, hdr[3], 1, f) == 1) {
            GLuint prog = glCreateProgram();
            if (prog) {
                glProgramBinary(prog, (GLenum)hdr[2], blob, (GLsizei)hdr[3]);
                GLint linked = 0;
                glGetProgramiv(prog, GL_LINK_STATUS, &linked);
                if (linked) {
                    *out_program = prog;
                    ok = true;
                } else {
                    /* Incompatible blob (driver changed under us): delete it */
                    glDeleteProgram(prog);
                }
            }
        }
    }
    free(blob);
    fclose(f);

    if (!ok) {
        unlink(path); /* stale/corrupt entry: drop so we re-store after compile */
    } else {
        log_debug("Program cache HIT: %s", path);
    }
    return ok;
}

void program_cache_store(uint64_t key, GLuint program) {
    cache_init_once();
    if (!g_supported || !program) return;

    /* Hint the driver we'll snapshot this program. Set BEFORE link normally;
     * setting after link still works on mesa/NVIDIA via re-snapshot. */
    glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

    GLint blob_len = 0;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &blob_len);
    if (blob_len <= 0) return;

    void *blob = malloc((size_t)blob_len);
    if (!blob) return;

    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(program, blob_len, &written, &format, blob);
    if (written <= 0 || glGetError() != GL_NO_ERROR) {
        free(blob);
        return;
    }

    char path[600], tmp_path[640];
    cache_path_for(key ^ g_driver_hash, path, sizeof(path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", path, getpid());

    FILE *f = fopen(tmp_path, "wb");
    if (f) {
        uint32_t hdr[4] = { CACHE_MAGIC, CACHE_VERSION, (uint32_t)format, (uint32_t)written };
        bool wrote = fwrite(hdr, sizeof(hdr), 1, f) == 1 &&
                     fwrite(blob, (size_t)written, 1, f) == 1;
        fclose(f);
        if (wrote) {
            rename(tmp_path, path); /* atomic publish */
            log_debug("Program cache STORE: %s (%d bytes)", path, written);
        } else {
            unlink(tmp_path);
        }
    }
    free(blob);
}
