#ifdef HAVE_WAYLAND_BACKEND

/* Hyprland-specific coverage detection.
 *
 * Wayland forbids cross-client geometry, so for the "mosaic of small windows
 * covers the wallpaper" case we have to talk to Hyprland directly. We open
 * its IPC socket, read `j/monitors` and `j/clients`, and rasterize the
 * windows on each output's active workspace into a coarse grid to compute
 * coverage. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <pthread.h>
#include "neowall.h"
#include "utils.h"
#include "hyprland_coverage.h"

#define HYPR_REFRESH_MS    500
#define GRID_W             64
#define GRID_H             64
#define MAX_MONITORS       16
#define MAX_CLIENTS        256

typedef struct {
    int id;
    char name[64];
    int x, y, w, h;
    int active_ws;
} hypr_monitor_t;

typedef struct {
    int monitor;
    int ws;
    int x, y, w, h;
    bool mapped;
    bool hidden;
    int fullscreen;
} hypr_client_t;

static pthread_mutex_t snap_lock = PTHREAD_MUTEX_INITIALIZER;
static hypr_monitor_t snap_monitors[MAX_MONITORS];
static int snap_n_monitors = 0;
static hypr_client_t snap_clients[MAX_CLIENTS];
static int snap_n_clients = 0;
static uint64_t last_refresh_ms = 0;
static int sock_cache = -1;

/* ---------- IPC ---------- */

static int open_hypr_socket(void) {
    const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (!his || !xdg) {
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    int n = snprintf(addr.sun_path, sizeof(addr.sun_path),
                     "%s/hypr/%s/.socket.sock", xdg, his);
    if (n <= 0 || (size_t)n >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Send a command, read the entire reply into a malloc'd buffer. Caller frees. */
static char *hypr_query(const char *cmd, size_t *out_len) {
    int fd = open_hypr_socket();
    if (fd < 0) {
        return NULL;
    }
    size_t cmd_len = strlen(cmd);
    if (send(fd, cmd, cmd_len, MSG_NOSIGNAL) != (ssize_t)cmd_len) {
        close(fd);
        return NULL;
    }
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        close(fd);
        return NULL;
    }
    for (;;) {
        if (len + 4096 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                close(fd);
                return NULL;
            }
            buf = nb;
        }
        ssize_t r = recv(fd, buf + len, cap - len - 1, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(buf);
            close(fd);
            return NULL;
        }
        if (r == 0) break;
        len += (size_t)r;
    }
    buf[len] = '\0';
    close(fd);
    if (out_len) *out_len = len;
    return buf;
}

/* ---------- minimal JSON scanner ---------- */

/* Skip whitespace. */
static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Find the value position for a key at the CURRENT nesting level only.
 * Returns pointer to the char after ':' (post-whitespace), or NULL.
 * `obj_start` must point at the opening '{'. */
static const char *find_key(const char *obj_start, const char *key) {
    if (!obj_start || *obj_start != '{') return NULL;
    const char *p = obj_start + 1;
    int depth = 0;
    size_t klen = strlen(key);
    bool in_str = false;
    while (*p) {
        if (in_str) {
            if (*p == '\\' && p[1]) { p += 2; continue; }
            if (*p == '"') in_str = false;
            p++;
            continue;
        }
        if (*p == '"') {
            /* possible key at depth 0 */
            const char *ks = p + 1;
            const char *ke = ks;
            while (*ke && *ke != '"') {
                if (*ke == '\\' && ke[1]) ke += 2;
                else ke++;
            }
            if (!*ke) return NULL;
            bool key_match = depth == 0 && (size_t)(ke - ks) == klen
                             && memcmp(ks, key, klen) == 0;
            p = ke + 1;
            p = skip_ws(p);
            if (*p == ':') {
                p++;
                p = skip_ws(p);
                if (key_match) return p;
                /* skip value */
                continue;
            }
            continue;
        }
        if (*p == '{' || *p == '[') { depth++; p++; continue; }
        if (*p == '}' || *p == ']') {
            if (depth == 0) return NULL;
            depth--;
            p++;
            continue;
        }
        p++;
    }
    return NULL;
}

static int parse_int_at(const char *p, int dflt) {
    if (!p) return dflt;
    p = skip_ws(p);
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return dflt;
    return (int)v;
}

/* Parse "[a, b]" array of two ints into out[0], out[1]. */
static bool parse_int_pair(const char *p, int out[2]) {
    if (!p) return false;
    p = skip_ws(p);
    if (*p != '[') return false;
    p++;
    char *end = NULL;
    out[0] = (int)strtol(p, &end, 10);
    if (end == p) return false;
    p = skip_ws(end);
    if (*p != ',') return false;
    p++;
    p = skip_ws(p);
    out[1] = (int)strtol(p, &end, 10);
    if (end == p) return false;
    return true;
}

static void parse_string_at(const char *p, char *out, size_t cap) {
    if (!p || !cap) { if (out && cap) out[0] = '\0'; return; }
    p = skip_ws(p);
    if (*p != '"') { out[0] = '\0'; return; }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) {
        if (*p == '\\' && p[1]) {
            out[i++] = p[1];
            p += 2;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
}

/* Iterate top-level objects in a JSON array. Returns next object start, or NULL.
 * On first call, pass the array start (pointing at '['). On subsequent calls,
 * pass the previously-returned object's end position (which we provide via
 * *cursor). */
static const char *next_array_obj(const char **cursor) {
    const char *p = *cursor;
    if (!p) return NULL;
    p = skip_ws(p);
    if (*p == '[') p++;
    p = skip_ws(p);
    if (*p == ',') { p++; p = skip_ws(p); }
    if (*p != '{') return NULL;
    const char *obj_start = p;
    int depth = 0;
    bool in_str = false;
    while (*p) {
        if (in_str) {
            if (*p == '\\' && p[1]) { p += 2; continue; }
            if (*p == '"') in_str = false;
            p++;
            continue;
        }
        if (*p == '"') { in_str = true; p++; continue; }
        if (*p == '{') { depth++; p++; continue; }
        if (*p == '}') {
            depth--;
            p++;
            if (depth == 0) {
                *cursor = p;
                return obj_start;
            }
            continue;
        }
        p++;
    }
    return NULL;
}

/* ---------- refresh ---------- */

static void parse_monitors(const char *json) {
    snap_n_monitors = 0;
    const char *cursor = json;
    const char *obj;
    while ((obj = next_array_obj(&cursor)) && snap_n_monitors < MAX_MONITORS) {
        hypr_monitor_t *m = &snap_monitors[snap_n_monitors];
        m->id = parse_int_at(find_key(obj, "id"), -1);
        parse_string_at(find_key(obj, "name"), m->name, sizeof(m->name));
        m->x = parse_int_at(find_key(obj, "x"), 0);
        m->y = parse_int_at(find_key(obj, "y"), 0);
        m->w = parse_int_at(find_key(obj, "width"), 0);
        m->h = parse_int_at(find_key(obj, "height"), 0);
        const char *ws = find_key(obj, "activeWorkspace");
        m->active_ws = parse_int_at(find_key(ws, "id"), -1);
        if (m->w > 0 && m->h > 0) {
            snap_n_monitors++;
        }
    }
}

static void parse_clients(const char *json) {
    snap_n_clients = 0;
    const char *cursor = json;
    const char *obj;
    while ((obj = next_array_obj(&cursor)) && snap_n_clients < MAX_CLIENTS) {
        hypr_client_t *c = &snap_clients[snap_n_clients];
        c->monitor = parse_int_at(find_key(obj, "monitor"), -1);
        const char *ws = find_key(obj, "workspace");
        c->ws = parse_int_at(find_key(ws, "id"), -1);
        int at[2] = {0, 0};
        int sz[2] = {0, 0};
        parse_int_pair(find_key(obj, "at"), at);
        parse_int_pair(find_key(obj, "size"), sz);
        c->x = at[0]; c->y = at[1]; c->w = sz[0]; c->h = sz[1];
        const char *mapped = find_key(obj, "mapped");
        const char *hidden = find_key(obj, "hidden");
        c->mapped = mapped && *mapped == 't';
        c->hidden = hidden && *hidden == 't';
        c->fullscreen = parse_int_at(find_key(obj, "fullscreen"), 0);
        if (c->w > 0 && c->h > 0 && c->mapped && !c->hidden) {
            snap_n_clients++;
        }
    }
}

bool hyprland_coverage_available(void) {
    return getenv("HYPRLAND_INSTANCE_SIGNATURE") != NULL;
}

void hyprland_coverage_refresh(void) {
    if (!hyprland_coverage_available()) {
        return;
    }
    uint64_t now = get_time_ms();
    pthread_mutex_lock(&snap_lock);
    if (now - last_refresh_ms < HYPR_REFRESH_MS) {
        pthread_mutex_unlock(&snap_lock);
        return;
    }
    last_refresh_ms = now;
    pthread_mutex_unlock(&snap_lock);

    /* Do network I/O outside the lock. */
    size_t mlen = 0, clen = 0;
    char *mjson = hypr_query("j/monitors", &mlen);
    char *cjson = hypr_query("j/clients", &clen);

    pthread_mutex_lock(&snap_lock);
    if (mjson) parse_monitors(mjson);
    if (cjson) parse_clients(cjson);
    pthread_mutex_unlock(&snap_lock);

    (void)sock_cache;
    free(mjson);
    free(cjson);
}

/* Find the monitor matching this output's connector name. */
static const hypr_monitor_t *find_monitor_for(const struct output_state *o) {
    const char *want = o->connector_name[0] ? o->connector_name : NULL;
    if (!want) return NULL;
    for (int i = 0; i < snap_n_monitors; i++) {
        if (strcmp(snap_monitors[i].name, want) == 0) {
            return &snap_monitors[i];
        }
    }
    return NULL;
}

bool hyprland_output_covered(const struct output_state *o, float threshold) {
    if (!o || !hyprland_coverage_available()) return false;

    pthread_mutex_lock(&snap_lock);

    const hypr_monitor_t *m = find_monitor_for(o);
    if (!m || m->w <= 0 || m->h <= 0) {
        pthread_mutex_unlock(&snap_lock);
        return false;
    }

    /* Rasterize windows on this monitor's active workspace into a grid. */
    static uint8_t grid[GRID_H][GRID_W];
    memset(grid, 0, sizeof(grid));

    for (int i = 0; i < snap_n_clients; i++) {
        const hypr_client_t *c = &snap_clients[i];
        if (c->monitor != m->id) continue;
        if (c->ws != m->active_ws) continue;

        /* Clip to monitor rect. Client coords are global. */
        int x0 = c->x, y0 = c->y;
        int x1 = c->x + c->w, y1 = c->y + c->h;
        if (x0 < m->x) x0 = m->x;
        if (y0 < m->y) y0 = m->y;
        if (x1 > m->x + m->w) x1 = m->x + m->w;
        if (y1 > m->y + m->h) y1 = m->y + m->h;
        if (x1 <= x0 || y1 <= y0) continue;

        /* Map monitor-local rect to grid cells. */
        int gx0 = (x0 - m->x) * GRID_W / m->w;
        int gy0 = (y0 - m->y) * GRID_H / m->h;
        int gx1 = (x1 - m->x) * GRID_W / m->w;
        int gy1 = (y1 - m->y) * GRID_H / m->h;
        if (gx1 > GRID_W) gx1 = GRID_W;
        if (gy1 > GRID_H) gy1 = GRID_H;
        for (int gy = gy0; gy < gy1; gy++) {
            for (int gx = gx0; gx < gx1; gx++) {
                grid[gy][gx] = 1;
            }
        }
    }

    int covered = 0;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            covered += grid[y][x];
        }
    }
    pthread_mutex_unlock(&snap_lock);

    float frac = (float)covered / (float)(GRID_W * GRID_H);
    return frac >= threshold;
}

#endif /* HAVE_WAYLAND_BACKEND */
