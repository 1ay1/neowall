/* Reactive system data for shaders.
 *
 * This module makes the wallpaper *alive*: it samples cheap system signals
 * (CPU/RAM/network load, battery, time-of-day, sun angle, input activity) and
 * captures system audio into an FFT spectrum. The shader engine exposes these
 * as extra GLSL uniforms and an audio iChannel, so a shader can react to what
 * the machine is actually doing — something Shadertoy fundamentally cannot do.
 *
 * Everything here is best-effort and degrades gracefully: missing /proc files,
 * no battery, no audio source → the corresponding signal reads 0 (or a sane
 * default) and nothing breaks. All sampling is throttled and runs on the main
 * loop thread except audio capture, which owns a dedicated thread.
 *
 * The values are published into a lock-free snapshot the render thread reads
 * once per frame. */

#ifndef NEOWALL_REACTIVE_H
#define NEOWALL_REACTIVE_H

#include <stdbool.h>
#include <stdint.h>

/* Number of FFT bins exposed to shaders (texture width). Power of two. */
#define REACTIVE_AUDIO_BINS 512

/* A frame-coherent snapshot of every reactive signal. Plain floats, copied by
 * value into the render path so the shader sees a consistent set each frame. */
typedef struct {
    /* --- load (0..1, smoothed) --- */
    float cpu;          /* total CPU utilisation */
    float cpu_per[8];   /* up to 8 per-core utilisations (0 if absent) */
    int   cpu_cores;    /* number of valid entries in cpu_per */
    float ram;          /* used / total physical memory */
    float swap;         /* used / total swap (0 if no swap) */
    float net_down;     /* normalised download rate (0..1, log-scaled) */
    float net_up;       /* normalised upload rate   (0..1, log-scaled) */
    float disk_read;    /* normalised disk read rate  (0..1, log-scaled) */
    float disk_write;   /* normalised disk write rate (0..1, log-scaled) */
    float load_avg;     /* 1-min load average / nproc, clamped 0..1 (raw can exceed) */
    float load_raw;     /* raw 1-min load average (not normalised) */

    /* --- thermals + GPU (best-effort; 0 if unavailable) --- */
    float cpu_temp;     /* CPU package temp, normalised 0..1 over 30..95 C */
    float cpu_temp_c;   /* CPU package temp in degrees C (0 if unknown) */
    float gpu;          /* GPU utilisation 0..1 (amdgpu/nvidia/i915 best-effort) */
    float gpu_temp;     /* GPU temp normalised 0..1 over 30..95 C */
    float gpu_temp_c;   /* GPU temp in degrees C (0 if unknown) */

    /* --- uptime + processes --- */
    float uptime_hours; /* system uptime in hours */
    float procs;        /* running/total process activity proxy 0..1 */
    int   proc_count;   /* total process/thread count (capped) */

    /* --- power --- */
    float battery;      /* charge fraction 0..1 (1 if no battery present) */
    bool  charging;     /* true if AC online / charging */

    /* --- time --- */
    float time_of_day;  /* 0..1 across the local day (0 = midnight) */
    float sun;          /* sun elevation proxy 0..1 (0 night, 1 noon) */
    float day_fraction; /* 0..1 across the year */

    /* --- input energy (0..1, decays) --- */
    float key_energy;   /* recent keyboard activity */
    float mouse_energy; /* recent mouse motion speed */

    /* --- audio --- */
    bool  audio_active;            /* true if a capture source is producing data */
    float audio_level;             /* overall RMS loudness 0..1 (smoothed) */
    float audio_bass;              /* low band energy 0..1 */
    float audio_mid;               /* mid band energy 0..1 */
    float audio_treble;            /* high band energy 0..1 */
    float audio_beat;              /* 0..1 beat pulse, spikes on onset, decays */
    /* Spectrum + waveform, each REACTIVE_AUDIO_BINS samples in 0..1.
     * Uploaded as the two rows of the audio iChannel texture. */
    float audio_spectrum[REACTIVE_AUDIO_BINS];
    float audio_waveform[REACTIVE_AUDIO_BINS];
} reactive_snapshot_t;

/* Initialise the subsystem. Safe to call once at startup. Starts the audio
 * capture thread if a source is available (best-effort). */
bool reactive_init(void);

/* Stop the audio thread and release resources. */
void reactive_shutdown(void);

/* Sample the cheap system signals (CPU/RAM/net/battery/time). Self-throttled:
 * call every frame, it only does real work a few times per second. Call on the
 * main loop thread. */
void reactive_sample(void);

/* Feed input-activity energy (called from pointer/key handlers). dt_ms is the
 * time since the last call; speed is in pixels for mouse. */
void reactive_note_key(void);
void reactive_note_mouse(float dx, float dy);

/* Copy the current frame-coherent snapshot. Cheap; call once per frame. */
void reactive_get(reactive_snapshot_t *out);

/* True if audio capture is live (a monitor source was opened). */
bool reactive_audio_available(void);

#endif /* NEOWALL_REACTIVE_H */
