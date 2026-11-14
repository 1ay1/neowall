/* Span groups: outputs showing one scene across several monitors cycle
 * together and share a virtual canvas. */

#include <stdio.h>
#include "render.h"
#include "output_internal.h"
#include "neowall/core/log.h"
#include "neowall/state/state.h"

/* ============================================
 * Span groups — one scene across several monitors
 * ============================================ */

/* The wallpaper an output is showing right now, "" if none. */
static const char *output_live_path(const struct output_state *o) {
    if (o->config->type == WALLPAPER_SHADER) {
        return o->config->shader_path;
    }
    return o->config->path;
}

/* Order-independent fingerprint of a cycle list.
 *
 * Two outputs pointed at the same directory hold equal lists, but not
 * necessarily in equal ORDER: shuffle randomises each output's copy separately
 * when it is loaded. Comparing slot by slot would therefore split a shuffled
 * pair into two groups of one and quietly disable spanning, so the entries are
 * summed rather than sequenced — the sum is unchanged by any permutation. */
static uint64_t cycle_list_digest(const struct wallpaper_config *c) {
    uint64_t total = 0;
    for (size_t i = 0; i < c->cycle_count; i++) {
        const char *p = c->cycle_paths[i];
        uint64_t h = 1469598103934665603ull;  /* FNV-1a 64 */
        for (; p && *p; p++) {
            h ^= (unsigned char)*p;
            h *= 1099511628211ull;
        }
        total += h;
    }
    return total;
}

bool output_same_span_group(const struct output_state *a, const struct output_state *b) {
    if (!a || !b || !a->config || !b->config) {
        return false;
    }
    const struct wallpaper_config *ca = a->config;
    const struct wallpaper_config *cb = b->config;

    /* Spanning is opt-in.  Merely choosing the same shader/image on independent
     * outputs must not couple their clocks, cycling, or virtual coordinates. */
    if (!ca->span || !cb->span) {
        return false;
    }

    bool a_named = ca->span_group[0] != '\0';
    bool b_named = cb->span_group[0] != '\0';
    if (a_named || b_named) {
        /* Named groups are the explicit authority and may intentionally start
         * on different entries; the existing group-sync path aligns them. */
        return a_named && b_named && ca->type == cb->type &&
               strcmp(ca->span_group, cb->span_group) == 0;
    }

    /* Anonymous span=true groups retain source-based compatibility semantics. */
    if (ca->type != cb->type || ca->cycle != cb->cycle) {
        return false;
    }
    if (ca->cycle) {
        if (ca->cycle_count != cb->cycle_count || !ca->cycle_paths || !cb->cycle_paths) {
            return false;
        }
        return cycle_list_digest(ca) == cycle_list_digest(cb);
    }
    return strcmp(output_live_path(a), output_live_path(b)) == 0;
}

void outputs_update_spans(struct output_state **outs, size_t count) {
    if (!outs || count == 0) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        struct output_state *me = outs[i];
        if (!me) {
            continue;
        }

        struct span_rect rects[NW_MAX_OUTPUTS];
        size_t n = 0;
        size_t self = 0;
        uint64_t start = 0;

        for (size_t j = 0; j < count && n < NW_MAX_OUTPUTS; j++) {
            struct output_state *o = outs[j];
            if (!o || !output_same_span_group(me, o)) {
                continue;
            }
            if (o == me) {
                self = n;
            }

            /* THE UNIT SEAM. x_offset/y_offset are LOGICAL on Wayland (they come
             * from xdg-output's logical_position) while width/height are the
             * PHYSICAL framebuffer (logical * scale). span_rect wants all four
             * logical.
             *
             * Prefer the compositor's own logical_size when we have it: that is
             * the exact box it laid the output out in. Reconstructing it as
             * width/scale is exact only under INTEGER scale, where the physical
             * size is a whole multiple of the logical one. Under fractional
             * scale the compositor rounds the framebuffer independently, so
             * width/scale is off by up to a pixel — enough to make two heads'
             * logical boxes disagree and seam the shared scene. Fall back to
             * width/scale when logical_size has not arrived (or the compositor
             * lacks zxdg_output_manager_v1). On X11 scale is 1, there is no
             * xdg logical size, and width/scale is the identity. */
            int32_t scale = output_normalized_scale(o);
            int32_t logical_w =
                o->xdg_logical_width > 0 ? o->xdg_logical_width : o->width / scale;
            int32_t logical_h =
                o->xdg_logical_height > 0 ? o->xdg_logical_height : o->height / scale;
            rects[n++] = (struct span_rect){
                .logical_x = o->x_offset,
                .logical_y = o->y_offset,
                .logical_w = logical_w,
                .logical_h = logical_h,
                .scale     = scale,
                /* The real framebuffer, so span_compute maps the logical box
                 * into exactly these pixels under fractional scale. */
                .device_w  = o->width,
                .device_h  = o->height,
            };

            /* Earliest start in the group, so every member's iTime agrees. A
             * member that loaded a moment later must not run a moment behind:
             * the seam between two halves of one scene shows any phase gap. */
            if (o->shader_start_time > 0 && (start == 0 || o->shader_start_time < start)) {
                start = o->shader_start_time;
            }
        }

        me->span_start_time = start;

        struct span_view v;
        if (n < 2 || !span_compute(rects, n, self, &v)) {
            me->spanned = false;
            continue;
        }
        /* Degenerate group: the box came out exactly this output's own size at
         * zero offset, which means every peer's rect lies inside this one's. The
         * spanned path would hand the shader this output's own resolution and no
         * offset — precisely what the standalone path already does — so short-
         * circuit it. Reached by a cloned/mirrored group (several outputs on one
         * logical region, equal or nested, as `xrandr --same-as` produces), where
         * each peer is separately either degenerate itself or given the sub-rect
         * of the box it actually covers.
         *
         * It is ALSO how a group whose rects were built from mismatched units
         * presents, because a swallowed neighbour nests exactly like a smaller
         * clone. The two are indistinguishable from the rects alone, so this is
         * logged rather than rejected: a wrongly-nested group is a bug at the
         * seam above, not something to detect down here. */
        if (v.virt_w == me->width && v.virt_h == me->height &&
            v.off_x == 0 && v.off_y == 0) {
            if (me->spanned) {
                log_debug("Output %s: no longer spanning, its logical rect "
                          "%dx%d+%d+%d (scale %d) contains every peer's in the group",
                          output_get_identifier(me),
                          rects[self].logical_w, rects[self].logical_h,
                          rects[self].logical_x, rects[self].logical_y, rects[self].scale);
            }
            me->spanned = false;
            continue;
        }
        if (!me->spanned) {
            log_debug("Output %s: spanning, drawing %dx%d+%d+%d of a %dx%d virtual screen",
                      output_get_identifier(me),
                      me->width, me->height, v.off_x, v.off_y, v.virt_w, v.virt_h);
        }
        me->span = v;
        me->spanned = true;
    }
}

/* Slot of `path` in a `count`-long cycle list, or `fallback` if it is absent.
 *
 * A shuffled cycle pass permutes the list in place when it wraps, so an index
 * captured before a pass no longer names the same entry after it. The entry that
 * is actually live is known by path, so its slot is re-found by string compare
 * rather than carried as an index. */
static size_t cycle_restore_index(char *const *paths, size_t count, const char *path,
                                  size_t fallback) {
    if (!paths || !path || path[0] == '\0') return fallback;

    for (size_t i = 0; i < count; i++) {
        if (paths[i] && strcmp(paths[i], path) == 0) return i;
    }

    return fallback;
}

/* Put `o` on exactly the wallpaper `leader` is showing, and point o's cycle
 * index at it, so the pair stays in step from here on.
 *
 * Only the leader ever steps a cycle; members are pushed its result. Their lists
 * hold the same entries but need not hold them in the same ORDER (shuffle
 * randomises each output's copy separately), so the leader's index means nothing
 * here and the entry is located in o's own list by path. */
static void output_adopt_group_wallpaper(struct output_state *o, struct output_state *leader) {
    char entry[NW_PATH_MAX];
    char live[NW_PATH_MAX];

    pthread_mutex_lock(&o->state->state_mutex);
    size_t index = leader->config->current_cycle_index;
    if (leader->config->cycle_paths && index < leader->config->cycle_count) {
        snprintf(entry, sizeof(entry), "%s", leader->config->cycle_paths[index]);
    } else {
        entry[0] = '\0';
    }
    snprintf(live, sizeof(live), "%s", output_live_path(leader));

    if (entry[0] != '\0' && o->config->cycle_paths) {
        o->config->current_cycle_index =
            cycle_restore_index(o->config->cycle_paths, o->config->cycle_count, entry,
                                o->config->current_cycle_index);
    }
    pthread_mutex_unlock(&o->state->state_mutex);

    if (live[0] == '\0') {
        return;
    }

    /* Remember the attempt before making it: if this shader compiles on the
     * leader's GPU context but not on this one, we must not queue it again on
     * every pass of the main loop. */
    snprintf(o->span_synced_path, sizeof(o->span_synced_path), "%s", live);

    if (o->config->type == WALLPAPER_SHADER) {
        if (strcmp(output_live_path(o), live) != 0 &&
            nw_is_err(output_set_shader(o, live))) {
            log_error("Failed to sync output %s onto its span group's shader: %s",
                      o->model[0] ? o->model : "unknown", live);
            return;
        }
        /* Shader + image cycling: the shader is fixed and the cycle list feeds
         * iChannel0, so the group is in step only once the image matches too. */
        if (entry[0] != '\0' && strcmp(entry, live) != 0 &&
            !render_update_channel_texture(o, 0, entry)) {
            log_error("Failed to sync iChannel0 on output %s: %s",
                      o->model[0] ? o->model : "unknown", entry);
            return;
        }
    } else {
        output_set_wallpaper(o, live);
    }

    atomic_store_explicit(&o->needs_redraw, true, memory_order_release);
}

void outputs_sync_span_group(struct output_state **outs, size_t count, size_t index) {
    if (!outs || index >= count || !outs[index]) {
        return;
    }
    struct output_state *me = outs[index];

    /* The first member in list order leads; everyone else follows it. */
    struct output_state *leader = NULL;
    for (size_t i = 0; i < count; i++) {
        if (outs[i] && output_same_span_group(me, outs[i])) {
            leader = outs[i];
            break;
        }
    }
    if (!leader || leader == me) {
        return;
    }

    const char *live = output_live_path(leader);
    if (live[0] == '\0' || strcmp(output_live_path(me), live) == 0) {
        return;
    }
    if (strcmp(me->span_synced_path, live) == 0) {
        return;  /* already tried this one and it did not take */
    }

    log_info("Syncing output %s onto its span group's wallpaper: %s",
             me->model[0] ? me->model : "unknown", live);
    output_adopt_group_wallpaper(me, leader);
}

size_t output_cycle_group(struct output_state **outs, size_t count, struct output_state *leader) {
    if (!leader) {
        return 0;
    }

    /* Membership is settled BEFORE the leader moves: a shuffled wrap permutes
     * the leader's list in place, and the members have not been given the new
     * order yet, so asking again afterwards could fail to recognise them. */
    struct output_state *members[NW_MAX_OUTPUTS];
    size_t n = 0;
    for (size_t i = 0; i < count && n < NW_MAX_OUTPUTS; i++) {
        if (outs && outs[i] && outs[i] != leader && output_same_span_group(outs[i], leader)) {
            members[n++] = outs[i];
        }
    }

    output_cycle_wallpaper(leader);

    for (size_t i = 0; i < n; i++) {
        members[i]->span_synced_path[0] = '\0';
        output_adopt_group_wallpaper(members[i], leader);
        /* The group moved together, so its members' cycle timers must restart
         * together too — otherwise each one fires again on its own schedule. */
        members[i]->last_cycle_time = leader->last_cycle_time;
    }

    return n + 1;
}
