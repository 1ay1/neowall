#ifndef NEOWALL_OUTPUT_FRAME_CLOCK_H
#define NEOWALL_OUTPUT_FRAME_CLOCK_H

/* Frame clock: what time should this frame show?
 *
 * Reading the wall clock when drawing starts gives iTime steps that wobble by
 * however much the draw start wobbles (a few ms at 60 Hz). Instead we stamp a
 * frame with the vblank it will be shown at: the compositor's present
 * feedback gives the vblank grid (phase + refresh), and we learn how many
 * refreshes after the nearest vblank a frame drawn now reaches the screen.
 * Consecutive frames then step by exactly one refresh.
 *
 * Pure logic, no GL or Wayland. All times are CLOCK_MONOTONIC ns. */

#include <stdbool.h>
#include <stdint.h>

#define NW_FRAME_CLOCK_VOTES 9

typedef struct {
    uint64_t grid_ns;       /* a known vblank (last present), 0 = none yet */
    uint64_t refresh_ns;    /* display refresh period, 0 = unknown */
    int      latency;       /* refreshes from the draw's nearest vblank to present */
    uint64_t last_target;   /* target handed out for the previous frame */
    int      votes[NW_FRAME_CLOCK_VOTES];   /* recent measured latencies */
    int      nvotes, vote_pos;
} nw_frame_clock;

void nw_frame_clock_init(nw_frame_clock *fc);

/* The present time to stamp a frame whose drawing starts at now_ns, or 0 when
 * the grid isn't known (caller falls back to the wall clock). Strictly
 * increasing across calls. *latency_used is what to hand back to
 * nw_frame_clock_presented() with this frame's feedback. */
uint64_t nw_frame_clock_target(nw_frame_clock *fc, uint64_t now_ns, int *latency_used);

/* Feedback for one frame: it was stamped with target_ns using latency_used
 * (target 0 = it wasn't stamped), and actually reached the screen at
 * present_ns. refresh_ns is the compositor's refresh (0 if it didn't say). */
void nw_frame_clock_presented(nw_frame_clock *fc, uint64_t target_ns, int latency_used,
                              uint64_t present_ns, uint64_t refresh_ns);

/* Forget the grid (output hidden for a long time, mode change). */
void nw_frame_clock_reset(nw_frame_clock *fc);

#endif /* NEOWALL_OUTPUT_FRAME_CLOCK_H */
