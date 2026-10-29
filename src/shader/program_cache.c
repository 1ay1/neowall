/* See neowall/shader/program_cache.h.
 *
 * $XDG_CACHE_HOME/neowall/programs/<key>.bin:
 *   u32 magic, u32 version, u32 format, u32 length, then the driver's blob.
 * Written to a temp file and renamed, so a crash never leaves half a file.
 * Kept to the newest MAX_ENTRIES files. */
#define _DEFAULT_SOURCE

#include "neowall/shader/program_cache.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "neowall/log.h"
#include "neowall/shader/platform_compat.h"

#define MAGIC 0x4E575042u /* "NWPB" */
#define VERSION 1u
#define MAX_BLOB (64u << 20)
#define MAX_ENTRIES 256

static int g_state = -1;   /* -1 unknown, 0 off, 1 on */
static char g_dir[512];
static uint64_t g_driver;
static unsigned g_hits, g_misses;

static uint64_t fnv1a(const char *s, uint64_t h) {
    for (; s && *s; s++) {
        h ^= (unsigned char)*s;
        h *= 0x100000001b3ull;
    }
    return h;
}

static bool mkdirs(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, 0700) != 0 && errno != EEXIST) return false;
        *p = '/';
    }
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static bool ready(void) {
    if (g_state >= 0) return g_state == 1;
    g_state = 0;
    if (getenv("NEOWALL_NO_PROGRAM_CACHE")) return false;
    GLint formats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
    while (glGetError() != GL_NO_ERROR) {}
    if (formats <= 0) {
        log_debug("program cache: the driver can't save programs");
        return false;
    }
    /* a driver update must never load an old blob */
    g_driver = fnv1a((const char *)glGetString(GL_VENDOR), 0xcbf29ce484222325ull);
    g_driver = fnv1a((const char *)glGetString(GL_RENDERER), g_driver);
    g_driver = fnv1a((const char *)glGetString(GL_VERSION), g_driver);
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    int n = xdg && xdg[0] == '/' ? snprintf(g_dir, sizeof(g_dir), "%s/neowall/programs", xdg)
            : home && home[0]    ? snprintf(g_dir, sizeof(g_dir), "%s/.cache/neowall/programs", home)
                                 : -1;
    if (n <= 0 || (size_t)n >= sizeof(g_dir)) return false;
    char tmp[sizeof(g_dir)];
    memcpy(tmp, g_dir, sizeof(tmp));
    if (!mkdirs(tmp)) {
        log_debug("program cache: can't create %s (%s)", g_dir, strerror(errno));
        return false;
    }
    g_state = 1;
    return true;
}

uint64_t program_cache_key(const char *vs, const char *fs) {
    return fnv1a(fs, fnv1a(vs, 0xcbf29ce484222325ull));
}

static void path_for(uint64_t key, char *out, size_t cap) {
    snprintf(out, cap, "%s/%016llx.bin", g_dir, (unsigned long long)(key ^ g_driver));
}

unsigned program_cache_load(uint64_t key) {
    if (!ready()) return 0;
    char path[640];
    path_for(key, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        g_misses++;
        return 0;
    }
    uint32_t h[4];
    GLuint prog = 0;
    void *blob = NULL;
    if (fread(h, sizeof(h), 1, f) == 1 && h[0] == MAGIC && h[1] == VERSION && h[3] && h[3] < MAX_BLOB &&
        (blob = malloc(h[3])) && fread(blob, h[3], 1, f) == 1) {
        prog = glCreateProgram();
        glProgramBinary(prog, (GLenum)h[2], blob, (GLsizei)h[3]);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        while (glGetError() != GL_NO_ERROR) {}
        if (!ok) {
            glDeleteProgram(prog);
            prog = 0;
        }
    }
    free(blob);
    fclose(f);
    if (!prog) {
        unlink(path); /* stale or corrupt: compile and store again */
        g_misses++;
        return 0;
    }
    utime(path, NULL); /* recently used: survives pruning */
    g_hits++;
    return prog;
}

/* Drop the oldest entries past MAX_ENTRIES. */
static void prune(void) {
    DIR *d = opendir(g_dir);
    if (!d) return;
    struct ent {
        char name[64];
        time_t at;
    } *ents = NULL;
    size_t n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        size_t len = strlen(de->d_name);
        if (len < 5 || len >= 64 || strcmp(de->d_name + len - 4, ".bin")) continue;
        char p[640];
        struct stat st;
        snprintf(p, sizeof(p), "%s/%s", g_dir, de->d_name);
        if (stat(p, &st) != 0) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            struct ent *ne = realloc(ents, cap * sizeof(*ents));
            if (!ne) break;
            ents = ne;
        }
        snprintf(ents[n].name, sizeof(ents[n].name), "%s", de->d_name);
        ents[n++].at = st.st_mtime;
    }
    closedir(d);
    while (n > MAX_ENTRIES) {
        size_t old = 0;
        for (size_t i = 1; i < n; i++)
            if (ents[i].at < ents[old].at) old = i;
        char p[640];
        snprintf(p, sizeof(p), "%s/%s", g_dir, ents[old].name);
        unlink(p);
        ents[old] = ents[--n];
    }
    free(ents);
}

void program_cache_store(uint64_t key, unsigned prog) {
    if (!prog || !ready()) return;
    GLint len = 0;
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0 || (uint32_t)len >= MAX_BLOB) return;
    void *blob = malloc((size_t)len);
    if (!blob) return;
    GLsizei got = 0;
    GLenum fmt = 0;
    glGetProgramBinary(prog, len, &got, &fmt, blob);
    if (got > 0 && glGetError() == GL_NO_ERROR) {
        char path[640], tmp[700];
        path_for(key, path, sizeof(path));
        snprintf(tmp, sizeof(tmp), "%s.%d.tmp", path, (int)getpid());
        FILE *f = fopen(tmp, "wb");
        if (f) {
            uint32_t h[4] = {MAGIC, VERSION, (uint32_t)fmt, (uint32_t)got};
            bool ok = fwrite(h, sizeof(h), 1, f) == 1 && fwrite(blob, (size_t)got, 1, f) == 1;
            ok = fclose(f) == 0 && ok;
            if (ok && rename(tmp, path) == 0) prune();
            else unlink(tmp);
        }
    }
    free(blob);
}

void program_cache_stats(unsigned *hits, unsigned *misses) {
    if (hits) *hits = g_hits;
    if (misses) *misses = g_misses;
}
