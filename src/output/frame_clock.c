/* Frame clock. See include/neowall/output/frame_clock.h. */

#include "neowall/output/frame_clock.h"

#include <string.h>

/* Until a frame's latency has been measured, assume it shows on the next
 * vblank after the nearest one. */
#define NW_FRAME_CLOCK_SEED_LATENCY 1
#define NW_FRAME_CLOCK_MAX_LATENCY 6

void nw_frame_clock_init(nw_frame_clock *fc) {
    memset(fc, 0, sizeof(*fc));
    fc->latency = NW_FRAME_CLOCK_SEED_LATENCY;
}

void nw_frame_clock_reset(nw_frame_clock *fc) {
    uint64_t last = fc->last_target;
    nw_frame_clock_init(fc);
    fc->last_target = last;   /* stay monotonic across the reset */
}

/* Nearest whole number of refreshes in a signed ns span. */
static int64_t round_div(int64_t a, int64_t b) {
    return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
}

uint64_t nw_frame_clock_target(nw_frame_clock *fc, uint64_t now_ns, int *latency_used) {
    if (latency_used) *latency_used = fc->latency;
    if (!fc->grid_ns || !fc->refresh_ns) return 0;

    const int64_t r = (int64_t)fc->refresh_ns;
    int64_t k = round_div((int64_t)(now_ns - fc->grid_ns), r);
    uint64_t ideal = fc->grid_ns + (uint64_t)((k + fc->latency) * r);

    /* Chain off the previous stamp in whole refreshes, so steps are exactly
     * N refreshes even when the measured grid wobbles. Resync to the grid
     * only if the chain has drifted from it by more than 1/8 refresh. */
    uint64_t target = ideal;
    if (fc->last_target) {
        int64_t n = round_div((int64_t)(ideal - fc->last_target), r);
        if (n < 1) n = 1;   /* always move forward */
        uint64_t chained = fc->last_target + (uint64_t)(n * r);
        int64_t drift = (int64_t)(ideal - chained);
        if (drift <= r / 8 && drift >= -r / 8) target = chained;
        else if (ideal <= fc->last_target) target = chained;
    }
    fc->last_target = target;
    return target;
}

/* Most common latency among recent frames. Ties keep the current value. */
static int vote_mode(const nw_frame_clock *fc) {
    int best = fc->latency, best_n = 0;
    for (int i = 0; i < fc->nvotes; i++) {
        int n = 0;
        for (int j = 0; j < fc->nvotes; j++) n += fc->votes[j] == fc->votes[i];
        if (n > best_n || (n == best_n && fc->votes[i] == fc->latency)) {
            best = fc->votes[i];
            best_n = n;
        }
    }
    return best;
}

void nw_frame_clock_presented(nw_frame_clock *fc, uint64_t target_ns, int latency_used,
                              uint64_t present_ns, uint64_t refresh_ns) {
    if (!present_ns) return;
    if (refresh_ns) fc->refresh_ns = refresh_ns;

    /* Keep the grid phase-locked rather than snapping to every present: the
     * compositor's timestamps wobble by a few ms now and then, and a real
     * panel at a fixed rate doesn't. Pull the grid 1/8 of the way toward a
     * present that lands near it; re-anchor only on a big miss (mode change,
     * long gap), where the old grid is simply wrong. */
    if (fc->grid_ns && fc->refresh_ns) {
        const int64_t r = (int64_t)fc->refresh_ns;
        int64_t d = (int64_t)(present_ns - fc->grid_ns);
        int64_t err = d - round_div(d, r) * r;          /* -r/2 .. r/2 */
        if (err > r / 4 || err < -r / 4) fc->grid_ns = present_ns;
        else fc->grid_ns = present_ns - (uint64_t)(err - err / 8);
    } else {
        fc->grid_ns = present_ns;
    }
    if (!target_ns || !fc->refresh_ns) return;

    /* How many refreshes later (or earlier) than stamped it really showed,
     * turned into the latency that frame actually had. A one-off missed
     * vblank is one vote; the mode ignores it. */
    int64_t off = round_div((int64_t)(present_ns - target_ns), (int64_t)fc->refresh_ns);
    int64_t lat = latency_used + off;
    if (lat < 0) lat = 0;
    if (lat > NW_FRAME_CLOCK_MAX_LATENCY) lat = NW_FRAME_CLOCK_MAX_LATENCY;

    fc->votes[fc->vote_pos] = (int)lat;
    fc->vote_pos = (fc->vote_pos + 1) % NW_FRAME_CLOCK_VOTES;
    if (fc->nvotes < NW_FRAME_CLOCK_VOTES) fc->nvotes++;
    fc->latency = vote_mode(fc);
}
