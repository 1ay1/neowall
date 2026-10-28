#ifndef NEOWALL_APP_H
#define NEOWALL_APP_H

/* The neowall program on the rt runtime (docs/architecture/RUNTIME.md).
 *
 * app_run() is the main loop: it builds the program and
 * host, wires signals, and runs the loop until the daemon is told to stop.
 * Everything that used to be an atomic request flag polled by the loop is
 * now a message folded by app_update(). */

#include <stdbool.h>
#include <stdint.h>

struct neowall_state;
struct output_state;

/* Message kinds. Values are stable only within one process. */
enum {
    NW_MSG_NONE = 0,
    NW_MSG_QUIT,            /* SIGINT/SIGTERM, or the display went away */
    NW_MSG_RELOAD,          /* SIGHUP: reload config */
    NW_MSG_NEXT,            /* SIGUSR1: advance every cycle group once */
    NW_MSG_CYCLE_PAUSE,     /* SIGUSR2 */
    NW_MSG_CYCLE_RESUME,    /* SIGCONT */
    NW_MSG_SET_INDEX,       /* SIGRTMIN: v.i = index (read from the index file) */
    NW_MSG_SHADER_PAUSE,    /* SIGRTMIN+1 */
    NW_MSG_SHADER_RESUME,   /* SIGRTMIN+2 */
    NW_MSG_SET_TERMINAL,    /* SIGRTMIN+3: command in the terminal file */
    NW_MSG_SIGNAL,          /* any other routed signal; id = signo */
    NW_MSG_CYCLE_DUE,       /* timer: a cycling output's duration elapsed; id = output */
    NW_MSG_FRAME_DUE,       /* timer: a vsync-off animated output's frame; id = output */
    NW_MSG_TRANSITION,      /* timer: a transition is running; redraw at 60 Hz */
    NW_MSG_DISPLAY,         /* the compositor fd had events (folded after dispatch) */
    NW_MSG_WATCH,           /* `neowall watch`: the shader file changed */
    NW_MSG_REDRAW,          /* any thread: redraw every output (rt_post) */
    NW_MSG_REDRAW_OUTPUT,   /* any thread: redraw one output; v.p = output */
    NW_MSG_OUTPUTS_NEW,     /* a new output appeared (hotplug / reconnect) */
};

/* Run neowall until it's told to stop. Takes over from main() once the
 * compositor, EGL and outputs are up. Returns the process exit code:
 * EXIT_SUCCESS, or NW_EXIT_GPU_LOST when graphics died. */
int app_run(struct neowall_state *state);

/* Wake the loop from another thread with a message. False if it isn't
 * running. Safe to call before app_run (the message is dropped). */
bool app_post(uint32_t kind, uint32_t id, int64_t value);

/* Redraw requests. Safe
 * from any thread. */
void app_request_redraw(struct neowall_state *state);
void app_request_output_redraw(struct output_state *output);

/* Ask the daemon to stop (any thread). */
void app_stop(struct neowall_state *state);

#endif /* NEOWALL_APP_H */
