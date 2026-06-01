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
#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include "neowall/neowall.h"
#include "hyprland_coverage.h"

#define HYPR_REFRESH_MS    500
#define HYPR_RECV_TIMEOUT_MS 200   /* cap blocking time per IPC call */
#define HYPR_SNAPSHOT_TTL_MS 3000  /* drop snapshot if no successful refresh in this long */
/* Grid resolution for rasterizing window rects on a monitor. Needs to be fine
 * enough that typical Hyprland gaps (~10-20 px) actually leave at least one
 * uncovered cell between adjacent tiles — otherwise inward-rounded edges
 * coincide and the gap is invisible to the coverage calculation. At 256x256
 * a cell on a 3440-wide monitor is ~13 px, which resolves single-digit gaps.
 * 256*256 = 64 KiB static buffer; trivially fits. */
#define GRID_W             256
#define GRID_H             256
#define MAX_MONITORS       16
#define MAX_CLIENTS        256

typedef struct {
    int id;
    char name[64];
    int x, y, w, h;
    /* reserved[0..3] = left, top, right, bottom — confirmed against
     * Hyprland's HyprCtl.cpp (`m_reservedArea.left/top/right/bottom`). */
    int reserved_left, reserved_top, reserved_right, reserved_bottom;
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
static uint64_t last_refresh_attempt_ms = 0; /* throttles retries */
static uint64_t last_refresh_ok_ms = 0;      /* freshness of snapshot */

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
    /* Don't let a wedged Hyprland stall the event loop. */
    struct timeval tv = {
        .tv_sec  = HYPR_RECV_TIMEOUT_MS / 1000,
        .tv_usec = (HYPR_RECV_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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
            /* Timeout or other error: treat as failure, not as truncated success. */
            free(buf);
            close(fd);
            return NULL;
        }
        if (r == 0) break;
        len += (size_t)r;
    }
    buf[len] = '\0';
    close(fd);

    /* Cheap structural sanity check: a complete j/monitors or j/clients reply
     * is a JSON array, so it must start with '[' and end with ']' (modulo
     * trailing whitespace). Anything else means we got a truncated read and
     * must NOT commit a partial parse over the previous good snapshot. */
    size_t end = len;
    while (end > 0 && (buf[end - 1] == ' ' || buf[end - 1] == '\t'
                       || buf[end - 1] == '\n' || buf[end - 1] == '\r')) {
        end--;
    }
    if (end < 2 || buf[0] != '[' || buf[end - 1] != ']') {
        free(buf);
        return NULL;
    }

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

/* Parse "[a, b, c, d]" array of four ints. Returns count actually parsed. */
static int parse_int_quad(const char *p, int out[4]) {
    for (int i = 0; i < 4; i++) out[i] = 0;
    if (!p) return 0;
    p = skip_ws(p);
    if (*p != '[') return 0;
    p++;
    int n = 0;
    while (n < 4) {
        p = skip_ws(p);
        if (*p == ']' || *p == '\0') break;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = skip_ws(end);
        if (*p == ',') { p++; continue; }
        if (*p == ']') break;
    }
    return n;
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
        /* `reserved`: [left, top, right, bottom] per Hyprland's HyprCtl.cpp.
         * Strip from the wallpaper region so layer-shell surfaces (waybar etc.)
         * don't count as "uncovered wallpaper" that drags the fraction below
         * the threshold. */
        int rsv[4] = {0, 0, 0, 0};
        parse_int_quad(find_key(obj, "reserved"), rsv);
        m->reserved_left   = rsv[0] > 0 ? rsv[0] : 0;
        m->reserved_top    = rsv[1] > 0 ? rsv[1] : 0;
        m->reserved_right  = rsv[2] > 0 ? rsv[2] : 0;
        m->reserved_bottom = rsv[3] > 0 ? rsv[3] : 0;
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
    if (now - last_refresh_attempt_ms < HYPR_REFRESH_MS) {
        pthread_mutex_unlock(&snap_lock);
        return;
    }
    /* Stamp the attempt timestamp so we throttle retries even on failure,
     * but DO NOT touch last_refresh_ok_ms until both queries succeed. The
     * previous code bumped the throttle before the queries ran, which meant
     * a failed IPC left the old (possibly "covered") snapshot in place for
     * 500ms with no retry — and if failures kept happening, the wallpaper
     * stayed paused indefinitely even after the user closed every window. */
    last_refresh_attempt_ms = now;
    pthread_mutex_unlock(&snap_lock);

    /* Do network I/O outside the lock. */
    size_t mlen = 0, clen = 0;
    char *mjson = hypr_query("j/monitors", &mlen);
    char *cjson = hypr_query("j/clients", &clen);

    pthread_mutex_lock(&snap_lock);
    if (mjson && cjson) {
        parse_monitors(mjson);
        parse_clients(cjson);
        last_refresh_ok_ms = now;
    }
    pthread_mutex_unlock(&snap_lock);

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

    /* If the snapshot is too old (IPC has been failing, Hyprland restarted,
     * socket got wedged, ...), don't trust it. Better to render an extra frame
     * than to leave the wallpaper frozen forever. */
    if (last_refresh_ok_ms == 0 ||
        get_time_ms() - last_refresh_ok_ms > HYPR_SNAPSHOT_TTL_MS) {
        pthread_mutex_unlock(&snap_lock);
        return false;
    }

    const hypr_monitor_t *m = find_monitor_for(o);
    if (!m || m->w <= 0 || m->h <= 0) {
        pthread_mutex_unlock(&snap_lock);
        return false;
    }

    /* Wallpaper region = monitor rect minus reserved zones (layer-shell
     * surfaces like waybar own those pixels; they are not user-visible
     * wallpaper and must not count toward the "uncovered" budget). */
    int wx0 = m->x + m->reserved_left;
    int wy0 = m->y + m->reserved_top;
    int wx1 = m->x + m->w - m->reserved_right;
    int wy1 = m->y + m->h - m->reserved_bottom;
    int ww = wx1 - wx0;
    int wh = wy1 - wy0;
    if (ww <= 0 || wh <= 0) {
        pthread_mutex_unlock(&snap_lock);
        return false;
    }

    /* Rasterize windows on this monitor's active workspace into a grid over
     * the WALLPAPER region. Use inward rounding (ceil on the low edge, floor
     * on the high edge) so a small inter-window gap actually leaves at least
     * one grid cell uncovered — otherwise both edges round to the same
     * boundary cell and the gap is silently absorbed. */
    static uint8_t grid[GRID_H][GRID_W];
    memset(grid, 0, sizeof(grid));

    for (int i = 0; i < snap_n_clients; i++) {
        const hypr_client_t *c = &snap_clients[i];
        if (c->monitor != m->id) continue;
        if (c->ws != m->active_ws) continue;

        /* Clip the client to the wallpaper region. */
        int x0 = c->x, y0 = c->y;
        int x1 = c->x + c->w, y1 = c->y + c->h;
        if (x0 < wx0) x0 = wx0;
        if (y0 < wy0) y0 = wy0;
        if (x1 > wx1) x1 = wx1;
        if (y1 > wy1) y1 = wy1;
        if (x1 <= x0 || y1 <= y0) continue;

        /* Map wallpaper-local rect to grid cells, rounding inward so we never
         * over-claim coverage at edges. (low: ceil; high: floor.) */
        int gx0 = ((x0 - wx0) * GRID_W + ww - 1) / ww;
        int gy0 = ((y0 - wy0) * GRID_H + wh - 1) / wh;
        int gx1 = ((x1 - wx0) * GRID_W) / ww;
        int gy1 = ((y1 - wy0) * GRID_H) / wh;
        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
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
