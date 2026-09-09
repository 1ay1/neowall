/* Virtual wall clock for shader authoring.
 *
 * Some of the most interesting wallpapers evolve over *days*: a garden whose
 * plants run a multi-day life cycle, a scene that drifts with the seasons, a
 * dashboard that accumulates history. Those shaders read the real calendar
 * (iDate, iTimeOfDay, iSun, iDayFraction), which makes them impossible to test
 * — verifying that a bloom opens on day 6 would mean waiting six days.
 *
 * This module puts one seam between the shader-visible clock and the system
 * clock. Every clock read that reaches a shader goes through nw_clock_now()
 * instead of time(NULL), so the daemon can offset it ("show me next Tuesday")
 * or run it fast ("30 days in 20 seconds") without touching the system clock or
 * the shader source.
 *
 * The clock affects ONLY shader-visible time. Timers, cycling, logging and
 * anything else that must track real time keep calling time()/clock_gettime()
 * directly. That separation is deliberate: time travel must never make the
 * daemon miss a cycle deadline or write a misleading log timestamp.
 *
 * Thread-safety: the offset/scale are atomics written on the main loop thread
 * and read by the render and reactive-sampling paths. Reads are relaxed; the
 * values change only on an explicit command, so a frame of skew is harmless. */

#ifndef NEOWALL_CLOCK_H
#define NEOWALL_CLOCK_H

#include <stdbool.h>
#include <time.h>

/* The shader-visible wall clock: real time, plus any offset, with elapsed time
 * since the clock was configured multiplied by the scale.
 *
 * With the default offset 0 / scale 1 this is exactly time(NULL). */
time_t nw_clock_now(void);

/* Shift the shader clock by `seconds` relative to real time. Positive jumps
 * forward. Replaces any previous offset. */
void nw_clock_set_offset(long seconds);

/* Run the shader clock at `scale` times real speed (1.0 = normal). The scale
 * applies to time elapsed since the call, so changing it never makes the clock
 * jump backwards. Values <= 0 are ignored. */
void nw_clock_set_scale(double scale);

/* Current offset in seconds and scale, for reporting. */
long   nw_clock_offset(void);
double nw_clock_scale(void);

/* True when the clock is not simply tracking real time, i.e. a preview or
 * timelapse is active. Used to warn in `neowall current` so a shifted clock is
 * never mistaken for a bug. */
bool nw_clock_is_virtual(void);

/* Parse a human offset like "+6d", "-2h", "90m", "3600" (bare = seconds) into
 * a second count. Suffixes: s, m, h, d, w. Returns false if unparseable. */
bool nw_clock_parse_offset(const char *spec, long *out_seconds);

/* Parse a timelapse spec "SPAN/DURATION", e.g. "30d/20s": compress SPAN of
 * shader time into DURATION of real time. Returns the resulting scale. */
bool nw_clock_parse_timelapse(const char *spec, double *out_scale);

#endif /* NEOWALL_CLOCK_H */
