/* Persistent per-shader state.
 *
 * A wallpaper with continuity — a garden that remembers how it grew, a scene
 * that accumulates weather — needs state that outlives the process. Feedback
 * buffers give a shader memory only while it runs: quit the daemon, or switch
 * wallpapers and come back, and every accumulated pixel is gone.
 *
 * This is a deliberately tiny store: 16 floats (4 x vec4) per shader, keyed by
 * the shader's path, saved under $XDG_STATE_HOME/neowall/shader-state/. Small
 * enough to write atomically and cheap to read at load; big enough to hold the
 * things that actually want to persist (a growth stage, a seed, a running
 * average, an event count).
 *
 * The shader sees `uniform vec4 iState[4]` and `uniform float iStateAge` (real
 * seconds since the values were written, so it can decide how much the world
 * should have moved on while it was away). To write state back, a shader
 * declares the `NW_STATE_WRITE` pass output; the daemon reads that back once a
 * second at most, so persistence never costs a per-frame stall.
 *
 * Everything degrades safely: unreadable or missing state reads as all zeros
 * with iStateAge = 0, which is exactly the "first run" case a shader must
 * already handle. */

#ifndef NEOWALL_SHADER_STATE_H
#define NEOWALL_SHADER_STATE_H

#include <stdbool.h>
#include <stdint.h>

/* Number of vec4 slots exposed to shaders as iState[]. */
#define NW_STATE_VEC4S 4
#define NW_STATE_FLOATS (NW_STATE_VEC4S * 4)

typedef struct {
    float  values[NW_STATE_FLOATS];
    int64_t saved_at;      /* unix seconds when written; 0 if never */
} nw_shader_state_t;

/* Load the state saved for `shader_path`. Always succeeds: a missing or
 * corrupt file yields all-zero values with saved_at = 0. */
void nw_shader_state_load(const char *shader_path, nw_shader_state_t *out);

/* Persist `state` for `shader_path`. Writes to a temp file and renames, so a
 * crash mid-write can never leave a half-written state file behind. Returns
 * false if the state directory could not be used. */
bool nw_shader_state_save(const char *shader_path, const nw_shader_state_t *state);

/* Seconds since the state was written, clamped at 0. Returns 0 when the state
 * has never been saved, so a first run reports "no time has passed". */
float nw_shader_state_age(const nw_shader_state_t *state);

#endif /* NEOWALL_SHADER_STATE_H */
