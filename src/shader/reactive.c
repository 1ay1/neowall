/* Reactive system data — implementation. See reactive.h.
 *
 * Zero new build dependencies. System metrics come from /proc and /sys. Audio
 * is captured by spawning `parec` (PulseAudio/PipeWire record) on a monitor
 * source and reading raw float32 mono PCM from its stdout on a worker thread;
 * if parec is absent the audio signals stay zero and everything else works.
 * The spectrum is computed with a small radix-2 FFT over a Hann window. */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "neowall/shader/reactive.h"
#include "neowall/neowall.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/wait.h>
#include <spawn.h>

extern char **environ;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- FFT config ---- */
#define FFT_SIZE 1024              /* must be power of two; bins = FFT_SIZE/2 */
#define SAMPLE_RATE 44100

/* ============================================================================
 * Shared state
 * ============================================================================ */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static reactive_snapshot_t g_snap;          /* protected by g_lock */
static atomic_bool g_inited = false;

/* audio thread */
static pthread_t g_audio_thread;
static atomic_bool g_audio_run = false;
static atomic_bool g_audio_live = false;
static pid_t g_parec_pid = -1;

/* input energy accumulators (written from input handlers, decayed in sample) */
static atomic_int g_key_hits = 0;
static _Atomic float g_mouse_accum = 0.0f;

/* ============================================================================
 * Small helpers
 * ============================================================================ */

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* read whole small file into buf; returns bytes read or -1 */
static long read_file(const char *path, char *buf, size_t cap) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    long total = 0;
    ssize_t n;
    while ((size_t)total < cap - 1 && (n = read(fd, buf + total, cap - 1 - total)) > 0) {
        total += n;
    }
    close(fd);
    if (total < 0) return -1;
    buf[total] = '\0';
    return total;
}

/* ============================================================================
 * CPU / RAM / NET / BATTERY / TIME
 * ============================================================================ */

typedef struct { unsigned long long idle, total; } cpu_times_t;

static bool parse_cpu_line(const char *line, cpu_times_t *out) {
    unsigned long long v[10] = {0};
    int got = sscanf(line, "cpu%*[^ ] %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
    if (got < 4) {
        /* try the aggregate "cpu " line (no suffix) */
        got = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
        if (got < 4) return false;
    }
    unsigned long long total = 0;
    for (int i = 0; i < 10; i++) total += v[i];
    out->idle = v[3] + v[4];   /* idle + iowait */
    out->total = total;
    return true;
}

static void sample_cpu(reactive_snapshot_t *s) {
    static cpu_times_t prev_total = {0};
    static cpu_times_t prev_core[8] = {{0}};

    char buf[4096];
    if (read_file("/proc/stat", buf, sizeof(buf)) <= 0) return;

    char *line = buf;
    int core = 0;
    bool first = true;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (strncmp(line, "cpu", 3) == 0) {
            cpu_times_t cur;
            bool is_agg = (line[3] == ' ');
            if (parse_cpu_line(line, &cur)) {
                if (is_agg && first) {
                    unsigned long long dt = cur.total - prev_total.total;
                    unsigned long long di = cur.idle - prev_total.idle;
                    if (dt > 0) s->cpu = clampf(1.0f - (float)di / (float)dt, 0.0f, 1.0f);
                    prev_total = cur;
                    first = false;
                } else if (!is_agg && core < 8) {
                    unsigned long long dt = cur.total - prev_core[core].total;
                    unsigned long long di = cur.idle - prev_core[core].idle;
                    if (dt > 0) s->cpu_per[core] = clampf(1.0f - (float)di / (float)dt, 0.0f, 1.0f);
                    prev_core[core] = cur;
                    core++;
                }
            }
        } else if (line != buf) {
            break; /* cpu lines are contiguous at the top of /proc/stat */
        }
        line = nl ? nl + 1 : NULL;
    }
    s->cpu_cores = core;
}

static void sample_ram(reactive_snapshot_t *s) {
    char buf[4096];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) <= 0) return;
    unsigned long long total = 0, avail = 0;
    char *p;
    if ((p = strstr(buf, "MemTotal:"))) sscanf(p, "MemTotal: %llu", &total);
    if ((p = strstr(buf, "MemAvailable:"))) sscanf(p, "MemAvailable: %llu", &avail);
    if (total > 0) s->ram = clampf(1.0f - (float)avail / (float)total, 0.0f, 1.0f);
}

static void sample_net(reactive_snapshot_t *s, double dt_sec) {
    static unsigned long long prev_rx = 0, prev_tx = 0;
    char buf[16384];
    if (read_file("/proc/net/dev", buf, sizeof(buf)) <= 0) return;

    unsigned long long rx = 0, tx = 0;
    char *line = strchr(buf, '\n');       /* skip 2 header lines */
    if (line) line = strchr(line + 1, '\n');
    if (line) line++;
    while (line && *line) {
        char iface[64];
        unsigned long long r = 0, t = 0;
        if (sscanf(line, " %63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",
                   iface, &r, &t) == 3) {
            if (strcmp(iface, "lo") != 0) { rx += r; tx += t; }
        }
        char *nl = strchr(line, '\n');
        line = nl ? nl + 1 : NULL;
    }

    if (prev_rx != 0 && dt_sec > 0.0) {
        double dn = (double)(rx - prev_rx) / dt_sec;   /* bytes/sec */
        double up = (double)(tx - prev_tx) / dt_sec;
        /* log-scale to 0..1: ~30 MB/s saturates */
        s->net_down = clampf((float)(log10(1.0 + dn) / log10(3.0e7)), 0.0f, 1.0f);
        s->net_up   = clampf((float)(log10(1.0 + up) / log10(3.0e7)), 0.0f, 1.0f);
    }
    prev_rx = rx; prev_tx = tx;
}

static void sample_battery(reactive_snapshot_t *s) {
    char buf[256];
    /* default: assume desktop (full, on AC) */
    s->battery = 1.0f;
    s->charging = true;
    for (int i = 0; i < 4; i++) {
        char path[128];
        snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/capacity", i);
        if (read_file(path, buf, sizeof(buf)) > 0) {
            s->battery = clampf((float)atoi(buf) / 100.0f, 0.0f, 1.0f);
            snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", i);
            if (read_file(path, buf, sizeof(buf)) > 0) {
                s->charging = (strncmp(buf, "Discharging", 11) != 0);
            }
            return;
        }
    }
}

static void sample_time(reactive_snapshot_t *s) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    float secs = tmv.tm_hour * 3600.0f + tmv.tm_min * 60.0f + tmv.tm_sec;
    s->time_of_day = secs / 86400.0f;
    /* sun elevation proxy: cosine peaking at solar noon (~13:00 local-ish) */
    float h = secs / 3600.0f;
    float sun = cosf((h - 13.0f) * (float)M_PI / 12.0f);
    s->sun = clampf((sun + 0.15f) / 1.15f, 0.0f, 1.0f);
    s->day_fraction = (float)tmv.tm_yday / 365.0f;
}

/* ============================================================================
 * Audio capture + FFT
 * ============================================================================ */

/* In-place iterative radix-2 FFT (Cooley-Tukey). re/im length n (power of 2). */
static void fft(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                float tr = cr * re[b] - ci * im[b];
                float ti = cr * im[b] + ci * re[b];
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

/* Spawn `parec` capturing the default monitor as float32 mono @ SAMPLE_RATE.
 * Returns a read fd for the PCM stream, or -1 on failure. */
static int spawn_parec(pid_t *out_pid) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    char rate[16];
    snprintf(rate, sizeof(rate), "%d", SAMPLE_RATE);
    char *argv[] = {
        "parec", "--format=float32le", "--channels=1", "--rate", rate,
        "--latency-msec=30", NULL
    };

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    posix_spawn_file_actions_addclose(&fa, pipefd[1]);

    pid_t pid;
    int rc = posix_spawnp(&pid, "parec", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        return -1;
    }
    *out_pid = pid;
    return pipefd[0];
}

static void *audio_thread_fn(void *arg) {
    (void)arg;
    /* Don't let SIGPIPE from a dying parec kill the process. */
    signal(SIGPIPE, SIG_IGN);

    pid_t pid = -1;
    int fd = spawn_parec(&pid);
    if (fd < 0) {
        log_info("Reactive: audio capture unavailable (parec not found) — "
                 "audio uniforms will read zero");
        atomic_store(&g_audio_live, false);
        return NULL;
    }
    g_parec_pid = pid;
    atomic_store(&g_audio_live, true);
    log_info("Reactive: audio capture active (parec monitor, %d Hz)", SAMPLE_RATE);

    float window[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));
    }

    float pcm[FFT_SIZE];
    int filled = 0;
    float beat_avg = 0.0f;       /* running average bass energy for onset detect */

    while (atomic_load(&g_audio_run)) {
        float chunk[256];
        ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got <= 0) {
            if (got < 0 && (errno == EINTR)) continue;
            break; /* parec died / EOF */
        }
        int samples = (int)(got / sizeof(float));
        for (int i = 0; i < samples; i++) {
            pcm[filled++] = chunk[i];
            if (filled < FFT_SIZE) continue;
            filled = 0;

            /* RMS loudness */
            float sum2 = 0.0f;
            for (int k = 0; k < FFT_SIZE; k++) sum2 += pcm[k] * pcm[k];
            float rms = sqrtf(sum2 / FFT_SIZE);

            float re[FFT_SIZE], im[FFT_SIZE];
            for (int k = 0; k < FFT_SIZE; k++) { re[k] = pcm[k] * window[k]; im[k] = 0.0f; }
            fft(re, im, FFT_SIZE);

            float spec[REACTIVE_AUDIO_BINS];
            float bass = 0, mid = 0, treble = 0;
            int bins = FFT_SIZE / 2; /* == REACTIVE_AUDIO_BINS */
            for (int k = 0; k < bins; k++) {
                float mag = sqrtf(re[k] * re[k] + im[k] * im[k]) / (FFT_SIZE / 2.0f);
                /* perceptual: log compress */
                float v = clampf(log10f(1.0f + mag * 40.0f), 0.0f, 1.0f);
                spec[k] = v;
                if (k < bins / 8)        bass += v;
                else if (k < bins / 2)   mid += v;
                else                     treble += v;
            }
            bass /= (bins / 8); mid /= (bins * 3 / 8); treble /= (bins / 2);

            /* simple beat: bass energy jumping above its running average */
            float beat = 0.0f;
            if (bass > beat_avg * 1.4f && bass > 0.15f) beat = 1.0f;
            beat_avg = beat_avg * 0.92f + bass * 0.08f;

            pthread_mutex_lock(&g_lock);
            float sm = 0.6f;
            g_snap.audio_active = true;
            g_snap.audio_level  = g_snap.audio_level  * sm + clampf(rms * 4.0f, 0, 1) * (1 - sm);
            g_snap.audio_bass   = g_snap.audio_bass   * sm + clampf(bass, 0, 1)       * (1 - sm);
            g_snap.audio_mid    = g_snap.audio_mid    * sm + clampf(mid, 0, 1)        * (1 - sm);
            g_snap.audio_treble = g_snap.audio_treble * sm + clampf(treble, 0, 1)     * (1 - sm);
            g_snap.audio_beat   = fmaxf(beat, g_snap.audio_beat * 0.85f);
            for (int k = 0; k < REACTIVE_AUDIO_BINS; k++) {
                g_snap.audio_spectrum[k] = g_snap.audio_spectrum[k] * 0.5f + spec[k] * 0.5f;
                g_snap.audio_waveform[k] = clampf(pcm[k * (FFT_SIZE / REACTIVE_AUDIO_BINS)] * 0.5f + 0.5f, 0, 1);
            }
            pthread_mutex_unlock(&g_lock);
        }
    }

    close(fd);
    if (g_parec_pid > 0) {
        kill(g_parec_pid, SIGTERM);
        waitpid(g_parec_pid, NULL, 0);
        g_parec_pid = -1;
    }
    atomic_store(&g_audio_live, false);
    return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

bool reactive_init(void) {
    if (atomic_load(&g_inited)) return true;
    memset(&g_snap, 0, sizeof(g_snap));
    g_snap.battery = 1.0f;
    g_snap.charging = true;

    atomic_store(&g_audio_run, true);
    if (pthread_create(&g_audio_thread, NULL, audio_thread_fn, NULL) != 0) {
        log_info("Reactive: could not start audio thread");
        atomic_store(&g_audio_run, false);
    }

    atomic_store(&g_inited, true);
    log_info("Reactive subsystem initialised");
    return true;
}

void reactive_shutdown(void) {
    if (!atomic_load(&g_inited)) return;
    if (atomic_load(&g_audio_run)) {
        atomic_store(&g_audio_run, false);
        if (g_parec_pid > 0) kill(g_parec_pid, SIGTERM);
        pthread_join(g_audio_thread, NULL);
    }
    atomic_store(&g_inited, false);
}

void reactive_sample(void) {
    if (!atomic_load(&g_inited)) return;

    static double last = 0.0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec / 1e9;
    double dt = now - last;
    if (last != 0.0 && dt < 0.25) return;   /* 4 Hz is plenty for these */
    double prev = last;
    last = now;

    pthread_mutex_lock(&g_lock);
    sample_cpu(&g_snap);
    sample_ram(&g_snap);
    sample_net(&g_snap, prev == 0.0 ? 0.0 : dt);
    sample_battery(&g_snap);
    sample_time(&g_snap);

    /* decay + fold in input energy */
    int keys = atomic_exchange(&g_key_hits, 0);
    float mouse = atomic_exchange(&g_mouse_accum, 0.0f);
    g_snap.key_energy   = clampf(g_snap.key_energy   * 0.80f + keys * 0.25f, 0.0f, 1.0f);
    g_snap.mouse_energy = clampf(g_snap.mouse_energy * 0.80f + mouse / 400.0f, 0.0f, 1.0f);

    if (!atomic_load(&g_audio_live)) {
        g_snap.audio_active = false;
    }
    pthread_mutex_unlock(&g_lock);
}

void reactive_note_key(void) {
    atomic_fetch_add(&g_key_hits, 1);
}

void reactive_note_mouse(float dx, float dy) {
    float d = sqrtf(dx * dx + dy * dy);
    /* relaxed atomic float add */
    float cur = atomic_load(&g_mouse_accum);
    while (!atomic_compare_exchange_weak(&g_mouse_accum, &cur, cur + d)) { }
}

void reactive_get(reactive_snapshot_t *out) {
    if (!out) return;
    if (!atomic_load(&g_inited)) { memset(out, 0, sizeof(*out)); out->battery = 1.0f; return; }
    pthread_mutex_lock(&g_lock);
    *out = g_snap;
    pthread_mutex_unlock(&g_lock);
}

bool reactive_audio_available(void) {
    return atomic_load(&g_audio_live);
}
