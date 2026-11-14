/* The host side of the loop: the compositor fd and the shader watch fd.
 * Each turns what the OS reports into messages for update. */


#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/app/watch.h"
#include "neowall/surface/surface.h"
#include "neowall/core/log.h"

/* ── host: the compositor fd ──────────────────────────────────────────── */

/* Before waiting: dispatch what's queued, flush, and tell rt whether the
 * backend already holds events poll() can't see (Xlib's queue). */
bool app_host_prepare(void *ctx, rt_loop *loop) {
    struct app *a = ctx;
    struct neowall_state *state = a->state;
    const compositor_backend_ops_t *ops = state->compositor_backend ?
                                          state->compositor_backend->ops : NULL;
    void *bd = state->compositor_backend ? state->compositor_backend->data : NULL;
    if (!ops) return false;

    a->host.read_prepared = false;
    if (ops->prepare_events) {
        if (!ops->prepare_events(bd)) {
            log_error("Failed to prepare compositor events");
            app_send(loop, NW_MSG_QUIT, 0, 0);
            return true;
        }
        a->host.read_prepared = true;
    }
    if (ops->flush) {
        compositor_flush_result_t fr = ops->flush(bd);
        if (fr == COMPOSITOR_FLUSH_FATAL) {
            log_error("Failed to flush compositor requests");
            app_send(loop, NW_MSG_QUIT, 0, 0);
            return true;
        }
        a->host.flush_blocked = fr == COMPOSITOR_FLUSH_BLOCKED;
        if (a->host.compositor_watch)
            rt_watch_modify(loop, a->host.compositor_watch,
                            RT_READ | (a->host.flush_blocked ? RT_WRITE : 0));
    }
    return ops->has_pending_events && ops->has_pending_events(bd);
}

void app_on_compositor(void *ctx, rt_loop *loop, int fd, unsigned ev) {
    (void)fd;
    struct app *a = ctx;
    struct neowall_state *state = a->state;
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *bd = state->compositor_backend->data;

    if (ev & RT_HANGUP) {
        log_error("Compositor disconnected, display server shut down");
        if (a->host.read_prepared && ops->cancel_read) ops->cancel_read(bd);
        a->host.read_prepared = false;
        app_send(loop, NW_MSG_QUIT, 0, 0);
        return;
    }
    if (ev & RT_READ) {
        if (ops->read_events && !ops->read_events(bd)) {
            log_error("Failed to read compositor events");
            app_send(loop, NW_MSG_QUIT, 0, 0);
            return;
        }
        a->host.read_prepared = false;
    }
    if ((ev & RT_WRITE) && a->host.flush_blocked && ops->flush) {
        compositor_flush_result_t fr = ops->flush(bd);
        if (fr == COMPOSITOR_FLUSH_FATAL) {
            log_error("Failed to flush compositor requests after POLLOUT");
            app_send(loop, NW_MSG_QUIT, 0, 0);
            return;
        }
        a->host.flush_blocked = fr == COMPOSITOR_FLUSH_BLOCKED;
    }
}

/* After every wait, whatever woke it: balance a prepared read (the fd
 * callback above already did, if the fd was readable), dispatch what's
 * queued (frame callbacks), and check for errors. */
void app_host_after_wait(void *ctx, rt_loop *loop, bool fd_ready) {
    (void)fd_ready;
    struct app *a = ctx;
    struct neowall_state *state = a->state;
    const compositor_backend_ops_t *ops = state->compositor_backend->ops;
    void *bd = state->compositor_backend->data;

    if (a->host.read_prepared && ops->cancel_read) ops->cancel_read(bd);
    a->host.read_prepared = false;

    if (ops->dispatch_events && !ops->dispatch_events(bd)) {
        log_error("Failed to dispatch compositor events, display server may have disconnected");
        app_send(loop, NW_MSG_QUIT, 0, 0);
        return;
    }
    if (ops->get_error) {
        int e = ops->get_error(bd);
        if (e != 0) {
            log_error("Compositor error (code %d), exiting", e);
            app_send(loop, NW_MSG_QUIT, 0, 0);
            return;
        }
    }
    rt_msg m = {.kind = NW_MSG_DISPLAY};
    rt_send(loop, &m);
}

void app_on_watch(void *ctx, rt_loop *loop, int fd, unsigned ev) {
    struct app *a = ctx;
    if (!(ev & RT_READ)) return;
    /* Always drain, even if the name doesn't match, or the fd stays ready. */
    if (watch_consume(fd) && a->state->watch_path[0]) {
        rt_msg m = {.kind = NW_MSG_WATCH};
        rt_send(loop, &m);
    }
}
