/* Hyprland coverage + window reporting, against a fake Hyprland IPC socket.
 *
 * A thread serves canned j/monitors and j/clients replies on
 * $XDG_RUNTIME_DIR/hypr/<sig>/.socket.sock, so the real IPC, the JSON scanner
 * and the coverage maths all run with no Hyprland. Covers: two monitors,
 * windows on other workspaces/monitors ignored, reserved zones (bars), a tiled
 * mosaic with gaps vs. without, clipping to the output, output-local coords,
 * truncated/garbage replies keeping the last good snapshot, and staleness.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "neowall/neowall.h"
#include "neowall/output/output.h"
#include "hyprland_coverage.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* What the fake answers; swapped by the test under the lock. */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *g_monitors, *g_clients;
static int g_listen = -1;
static atomic_int g_stop;

static void *serve(void *arg) {
    (void)arg;
    while (!g_stop) {
        int c = accept(g_listen, NULL, NULL);
        if (c < 0) continue;
        char cmd[64] = {0};
        ssize_t n = recv(c, cmd, sizeof(cmd) - 1, 0);
        pthread_mutex_lock(&g_lock);
        const char *reply = (n > 0 && strstr(cmd, "monitors")) ? g_monitors : g_clients;
        if (reply) {
            ssize_t w = send(c, reply, strlen(reply), MSG_NOSIGNAL);
            (void)w;
        }
        pthread_mutex_unlock(&g_lock);
        close(c);
    }
    return NULL;
}

static void serve_json(const char *mon, const char *cli) {
    pthread_mutex_lock(&g_lock);
    g_monitors = mon;
    g_clients = cli;
    pthread_mutex_unlock(&g_lock);
}

/* Force a fresh snapshot (the real one is throttled to 500ms). */
static void refresh(void) {
    usleep(510 * 1000);
    hyprland_coverage_refresh();
}

static struct output_state *out_named(struct output_state *o, const char *name) {
    memset(o, 0, sizeof(*o));
    snprintf(o->connector_name, sizeof(o->connector_name), "%s", name);
    return o;
}

/* Two 1000x500 monitors side by side; DP-1 has a 30px bar on top. */
static const char MONITORS[] =
    "[{\"id\": 0, \"name\": \"DP-1\", \"x\": 0, \"y\": 0, \"width\": 1000, \"height\": 500,"
    "  \"activeWorkspace\": {\"id\": 1, \"name\": \"1\"}, \"reserved\": [0, 30, 0, 0]},"
    " {\"id\": 1, \"name\": \"HDMI-A-1\", \"x\": 1000, \"y\": 0, \"width\": 1000, \"height\": 500,"
    "  \"activeWorkspace\": {\"id\": 2, \"name\": \"2\"}, \"reserved\": [0, 0, 0, 0]}]";

#define CLIENT(mon, ws, x, y, w, h, extra) \
    "{\"address\": \"0x1\", \"mapped\": true, \"hidden\": false, \"at\": [" #x ", " #y "]," \
    " \"size\": [" #w ", " #h "], \"workspace\": {\"id\": " #ws ", \"name\": \"x\"}," \
    " \"monitor\": " #mon ", \"title\": \"a \\\"quoted\\\" {title}\", \"fullscreen\": " extra "}"

int main(void) {
    char dir[] = "/tmp/nw-hypr-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char path[64];
    snprintf(path, sizeof(path), "%s/hypr/testsig", dir);
    char hyprdir[64];
    snprintf(hyprdir, sizeof(hyprdir), "%s/hypr", dir);
    mkdir(hyprdir, 0700);
    mkdir(path, 0700);
    setenv("XDG_RUNTIME_DIR", dir, 1);
    setenv("HYPRLAND_INSTANCE_SIGNATURE", "testsig", 1);

    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof(a.sun_path), "%s/.socket.sock", path);
    g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen < 0 || bind(g_listen, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(g_listen, 8) != 0) {
        perror("fake hyprland socket");
        return 1;
    }
    pthread_t th;
    pthread_create(&th, NULL, serve, NULL);

    struct output_state dp, hdmi, other;
    out_named(&dp, "DP-1");
    out_named(&hdmi, "HDMI-A-1");
    out_named(&other, "eDP-9");
    nw_window_rect w[8];

    CHECK(hyprland_coverage_available(), "available with the env var");

    /* 1. One window on DP-1 covering the left half below the bar; one on a
     *    hidden workspace; one on the other monitor. */
    static const char c1[] = "[" CLIENT(0, 1, 0, 30, 500, 470, "0") ","
                             CLIENT(0, 7, 0, 0, 1000, 500, "0") ","
                             CLIENT(1, 2, 1100, 50, 200, 100, "1") "]";
    serve_json(MONITORS, c1);
    refresh();
    CHECK(!hyprland_output_covered(&dp, 0.6f), "half covered < 60%%");
    CHECK(hyprland_output_covered(&dp, 0.45f), "half covered >= 45%% (bar excluded)");
    int n = hyprland_output_windows(&dp, w, 8);
    CHECK(n == 1, "one window on DP-1's active workspace, got %d", n);
    CHECK(n == 1 && w[0].x == 0 && w[0].y == 30 && w[0].w == 500 && w[0].h == 470,
          "rect %.0f,%.0f %.0fx%.0f", w[0].x, w[0].y, w[0].w, w[0].h);
    n = hyprland_output_windows(&hdmi, w, 8);
    CHECK(n == 1 && w[0].x == 100 && w[0].y == 50, "output-local coords on HDMI: %.0f,%.0f", w[0].x, w[0].y);
    CHECK(n == 1 && w[0].focused, "fullscreen window reported focused");
    CHECK(hyprland_output_windows(&other, w, 8) == 0, "unknown output sees no windows");
    CHECK(hyprland_output_windows(&dp, w, 0) == 0, "max 0 writes nothing");

    /* 2. Full tiling with no gaps covers; the same tiling with 20px gaps
     *    leaves strips that keep it under 100%. */
    static const char tiled[] = "[" CLIENT(0, 1, 0, 30, 500, 470, "0") "," CLIENT(0, 1, 500, 30, 500, 470, "0") "]";
    static const char gapped[] = "[" CLIENT(0, 1, 20, 50, 470, 430, "0") "," CLIENT(0, 1, 510, 50, 470, 430, "0") "]";
    serve_json(MONITORS, tiled);
    refresh();
    CHECK(hyprland_output_covered(&dp, 1.0f), "gapless tiling covers 100%%");
    serve_json(MONITORS, gapped);
    refresh();
    CHECK(!hyprland_output_covered(&dp, 1.0f), "gaps are not covered");
    CHECK(hyprland_output_covered(&dp, 0.8f), "but most of it is");

    /* 3. A window hanging off the monitor is clipped to it. */
    static const char hang[] = "[" CLIENT(0, 1, 900, 400, 400, 400, "0") "]";
    serve_json(MONITORS, hang);
    refresh();
    n = hyprland_output_windows(&dp, w, 8);
    CHECK(n == 1 && w[0].x == 900 && w[0].w == 100 && w[0].h == 100,
          "clipped to the output: %.0f %.0fx%.0f", w[0].x, w[0].w, w[0].h);

    /* 4. Garbage or a truncated reply must not replace the good snapshot. */
    serve_json(MONITORS, "[{\"id\": 0, \"at\": [1, ");
    refresh();
    n = hyprland_output_windows(&dp, w, 8);
    CHECK(n == 1 && w[0].x == 900, "truncated reply keeps the last snapshot");
    serve_json("not json", "nope");
    refresh();
    CHECK(hyprland_output_windows(&dp, w, 8) == 1, "garbage keeps the last snapshot");
    serve_json("[]", "[]");
    refresh();
    CHECK(hyprland_output_windows(&dp, w, 8) == 0, "empty but valid replies clear it");

    /* 5. Stale: once IPC stops answering, after the TTL nothing is reported. */
    serve_json(MONITORS, hang);
    refresh();
    CHECK(hyprland_output_windows(&dp, w, 8) == 1, "fresh again");
    serve_json(NULL, NULL);   /* the fake now answers with nothing */
    usleep(3300 * 1000);      /* past HYPR_SNAPSHOT_TTL_MS (3000) */
    hyprland_coverage_refresh();
    CHECK(hyprland_output_windows(&dp, w, 8) == 0, "stale snapshot reports no windows");
    CHECK(!hyprland_output_covered(&dp, 0.01f), "stale snapshot is never 'covered'");

    /* 6. Without the env var it's off. */
    unsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    CHECK(!hyprland_coverage_available(), "off without the env var");
    CHECK(hyprland_output_windows(&dp, w, 8) == 0, "no windows without Hyprland");

    g_stop = 1;
    shutdown(g_listen, SHUT_RDWR);
    close(g_listen);
    int wake = socket(AF_UNIX, SOCK_STREAM, 0);   /* unblock accept */
    if (connect(wake, (struct sockaddr *)&a, sizeof(a)) == 0) close(wake);
    pthread_join(th, NULL);
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fails++;

    printf("hyprland_coverage: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
