/* Frame pacing: vsync, the frame timer for vsync-off outputs, and phase
 * locking to the compositor's presentation feedback. */

#include <errno.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "neowall/surface/surface.h"
#include "neowall/egl/egl_core.h"
#include "output_internal.h"
#include "neowall/core/log.h"
#include "neowall/state/state.h"

/* Helper function to configure vsync (swap interval) for shader rendering
 * DRY principle: Single source of truth for vsync configuration logic */
void output_configure_vsync(struct output_state *output) {
    if (!output || !output->compositor_surface ||
        output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        return;
    }

    /* Make EGL context current */
    if (!egl_make_surface_current(output->state->egl_display, output->compositor_surface->egl_surface, output->state->egl_context)) {
        log_error("Failed to make EGL context current for vsync config");
        return;
    }

    /* Configure vsync based on user preference:
     * - vsync=false: a timerfd paces to shader_fps and tearing is permitted.
     * - vsync=true on Wayland: each output is paced by its own frame callback,
     *   and its swap must not block: a blocking swap on one monitor stalls the
     *   rest (#85). So the interval is 0 there.
     * - vsync=true without frame callbacks (X11): interval 1, so presents land
     *   on the refresh; the frame timer paces the loop at the refresh rate. */
    bool paced = output->config->vsync &&
                 compositor_surface_supports_frame_pacing(output->compositor_surface);
    int swap_interval = (output->config->vsync && !paced) ? 1 : 0;

    if (!eglSwapInterval(output->state->egl_display, swap_interval)) {
        EGLint err = eglGetError();
        log_error("Failed to set swap interval to %d (error: 0x%x)", swap_interval, err);
        if (!output->config->vsync) {
            log_error("This may prevent achieving target FPS of %d", output->config->shader_fps);
        }
    } else {
        if (output->config->vsync) {
            log_debug("Enabled vsync for output %s (will sync to monitor refresh rate)",
                     output->model[0] ? output->model : "unknown");
        } else {
            log_debug("Disabled vsync for output %s (shader_fps=%d, target frame time: %.1fms)",
                     output->model[0] ? output->model : "unknown",
                     output->config->shader_fps,
                     1000.0f / output->config->shader_fps);
        }
    }

    /* Keep the compositor's presentation hint in agreement with the EGL swap
     * interval. The surface is created before a config is attached to the
     * output, so the hint chosen at creation time can be stale by now; setting
     * eglSwapInterval(1) while the surface still says "async" leaves the
     * compositor free to scan out early and the wallpaper still jerks. */
    compositor_surface_set_vsync(output->compositor_surface, output->config->vsync);
}

/* Helper function to configure high-precision frame timer for vsync-off mode
 * Uses timerfd for kernel-level precision instead of poll() timeout */
bool output_configure_frame_timer(struct output_state *output) {
    if (!output) {
        return false;
    }

    /* If vsync is enabled and the backend paces each output by its own frame
     * callback (Wayland), no frame timer is needed. Without callbacks (X11) a
     * vsync swap would be the only pacing, and it can block the loop or not
     * block at all (measured on Xwayland: 2 fps on the old loop, 280 on the
     * new). There the timer paces it too: vsync still keeps each present on
     * the refresh, the timer keeps the loop at the refresh rate. */
    bool paced_by_callback = compositor_surface_supports_frame_pacing(output->compositor_surface);
    if (output->config->vsync && paced_by_callback) {
        if (output->frame_timer_fd >= 0) {
            close(output->frame_timer_fd);
            output->frame_timer_fd = -1;
            log_debug("Closed frame timer for output %s (vsync enabled)", output_get_identifier(output));
        }
        return true;
    }

    /* For animated wallpapers (shader or terminal) with vsync disabled, set up
     * a precise frame timer. Static images don't need one. */
    if (!wallpaper_is_animated(output->config->type)) {
        if (output->frame_timer_fd >= 0) {
            close(output->frame_timer_fd);
            output->frame_timer_fd = -1;
        }
        return true;
    }

    /* Create timerfd if not already created */
    if (output->frame_timer_fd < 0) {
        output->frame_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (output->frame_timer_fd < 0) {
            log_error("Failed to create frame timer for output %s: %s",
                     output_get_identifier(output), strerror(errno));
            return false;
        }
        log_debug("Created frame timer fd=%d for output %s",
                 output->frame_timer_fd, output_get_identifier(output));
    }

    /* Configure timer for target FPS */
    int target_fps = shader_fps_resolve(output->config->shader_fps);
    
    /* Calculate interval in seconds and nanoseconds
     * tv_nsec must be < 1000000000 (less than 1 second) */
    time_t interval_sec = 0;
    long interval_ns = 0;
    
    if (target_fps >= 1) {
        /* For FPS >= 1, interval is less than or equal to 1 second */
        long total_ns = 1000000000L / target_fps;
        interval_sec = total_ns / 1000000000L;
        interval_ns = total_ns % 1000000000L;
        
        /* Handle the edge case where FPS=1 results in exactly 1 second */
        if (interval_ns == 0 && target_fps == 1) {
            interval_sec = 1;
            interval_ns = 0;
        }
    } else {
        /* FPS < 1 means interval > 1 second (e.g., 0.5 FPS = 2 seconds) */
        interval_sec = 1;
        interval_ns = 0;
    }

    struct itimerspec timer_spec = {
        .it_interval = { .tv_sec = interval_sec, .tv_nsec = interval_ns },  /* Recurring */
        .it_value = { .tv_sec = interval_sec, .tv_nsec = interval_ns }      /* Initial expiration */
    };

    /* Record the target period for the phase-locked pacer. The recurring
     * interval above is only the BOOTSTRAP arming; from the first present
     * onward output_pace_advance() re-arms this fd as a one-shot absolute
     * deadline, so the schedule tracks real completion times instead of
     * free-running (see output.h pace_* fields). Seed the deadline to "now +
     * one period" so the very first re-arm has a sane anchor. */
    output->pace_period_ns = (uint64_t)interval_sec * 1000000000ULL + (uint64_t)interval_ns;
    {
        struct timespec now_ts;
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + (uint64_t)now_ts.tv_nsec;
        output->pace_next_deadline_ns = now_ns + output->pace_period_ns;
    }

    if (timerfd_settime(output->frame_timer_fd, 0, &timer_spec, NULL) < 0) {
        log_error("Failed to set frame timer for output %s: %s",
                 output_get_identifier(output), strerror(errno));
        return false;
    }

    log_debug("Configured frame timer for %d FPS (interval: %ld.%09ld s) on output %s",
             target_fps, (long)interval_sec, interval_ns, output_get_identifier(output));

    return true;
}

/**
 * Destroy a texture
 * Wrapper around render_destroy_texture()
 */

/**
 * Get frame timer file descriptor for precise frame pacing
 * Returns -1 if timer not active (vsync enabled or not a shader)
 */
int output_get_frame_timer_fd(struct output_state *output) {
    if (!output) {
        return -1;
    }
    return output->frame_timer_fd;
}

/* Phase-locked frame pacer: re-arm the per-output frame timer as a ONE-SHOT
 * ABSOLUTE deadline exactly one period after the previous target, instead of
 * letting a free-running recurring interval drift out of phase with the
 * display.
 *
 * Called once per output right after its buffer swap completes. `now_ns` is a
 * CLOCK_MONOTONIC timestamp captured just after the swap (the best available
 * phase anchor for when the frame actually reached the compositor without the
 * full wp_presentation feedback path).
 *
 * Behaviour:
 *   - advance the deadline by exactly one period (preserves average FPS with
 *     zero long-term drift — errors don't accumulate);
 *   - if we've fallen more than one period behind (a slow frame, a stall, or
 *     the app was just unpaused), SNAP the deadline forward to now+period so we
 *     drop the backlog instead of firing a burst of catch-up frames;
 *   - arm with TFD_TIMER_ABSTIME so the kernel wakes us at the true wall-clock
 *     instant, absorbing poll()/scheduler latency that a relative timer bakes
 *     into every subsequent frame.
 *
 * No-op (returns false) if the pacer is inactive (vsync on, static wallpaper,
 * or no frame timer) — those paths keep their existing scheduling. */
bool output_pace_advance(struct output_state *output, uint64_t now_ns) {
    if (!output || output->frame_timer_fd < 0 || output->pace_period_ns == 0) {
        return false;
    }

    uint64_t period = output->pace_period_ns;

    /* If the compositor has told us the display's TRUE refresh period via
     * wp_presentation, quantise our period to a whole number of refreshes so we
     * present exactly once every N vblanks (e.g. a 60fps request on a 59.94Hz
     * panel locks to 59.94, not a fractional cadence that beats against it).
     * Never below one refresh. */
    if (output->pace_hw_refresh_ns > 0) {
        uint64_t hw = output->pace_hw_refresh_ns;
        uint64_t mult = (period + hw / 2) / hw;      /* round to nearest N vblanks */
        if (mult < 1) mult = 1;
        period = mult * hw;
    }

    /* Anchor the schedule to the LAST REAL PRESENT when we have one (ground
     * truth for where the display's phase actually is), otherwise to our own
     * previous deadline. Advancing by exactly one period keeps average FPS with
     * zero long-term drift. */
    uint64_t anchor = output->pace_last_present_ns ? output->pace_last_present_ns
                                                   : output->pace_next_deadline_ns;
    uint64_t deadline = anchor + period;

    /* Overrun / stale-anchor recovery: if the next target is already in the
     * past (slow frame, stall, just-unpaused, or a present report we haven't
     * caught up to), resync the phase to the present rather than firing a burst
     * of catch-up frames the display can't show. */
    if (deadline <= now_ns) {
        deadline = now_ns + period;
    }
    output->pace_next_deadline_ns = deadline;

    struct itimerspec ts = {
        .it_interval = { .tv_sec = 0, .tv_nsec = 0 },   /* one-shot: we re-arm each present */
        .it_value = {
            .tv_sec  = (time_t)(deadline / 1000000000ULL),
            .tv_nsec = (long)(deadline % 1000000000ULL),
        },
    };
    if (timerfd_settime(output->frame_timer_fd, TFD_TIMER_ABSTIME, &ts, NULL) < 0) {
        /* Fall back to the free-running arming already in place; not fatal. */
        return false;
    }
    return true;
}

/* Record a real present event from wp_presentation feedback. `present_ns` is
 * the compositor-reported present timestamp converted to nanoseconds in the
 * presentation clock; `refresh_ns` is the display's refresh period (may be 0 if
 * the compositor didn't report one, e.g. a variable-refresh present). These
 * become the pacer's phase anchor + quantisation base on the next re-arm.
 *
 * The presentation clock is CLOCK_MONOTONIC in practice (compositors advertise
 * it via wp_presentation.clock_id; the caller only routes feedback here when it
 * matches our timer clock), so present_ns is directly comparable to the
 * absolute deadlines the frame timer is armed with. */
void output_pace_note_present(struct output_state *output,
                              uint64_t present_ns, uint64_t refresh_ns) {
    if (!output) return;
    output->pace_last_present_ns = present_ns;
    if (refresh_ns > 0) {
        output->pace_hw_refresh_ns = refresh_ns;
    }
}
