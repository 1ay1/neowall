#ifndef NEOWALL_GPU_HEALTH_H
#define NEOWALL_GPU_HEALTH_H

#include <stdbool.h>
#include <stdint.h>
#include <EGL/egl.h>

/* GPU health: robust contexts, reset detection, and driver debug output.
 *
 * A GL context that went through a GPU reset (driver crash, suspend on some
 * drivers, a runaway shader killed by the kernel watchdog) can't be trusted:
 * every object in it may be gone. The safe recovery is a fresh process, so
 * neowall detects the loss and exits with NW_EXIT_GPU_LOST; systemd's
 * Restart=on-failure (or the daemon's own re-exec) brings it back clean. */

/* sysexits EX_TEMPFAIL: "try again", which is exactly what a restart does. */
#define NW_EXIT_GPU_LOST 75

/* Swaps that fail in a row before the surface is treated as gone. */
#define NW_GPU_SWAP_FAIL_LIMIT 30

/* Context attributes asking for robustness when the display supports it.
 * Fills `attribs` (EGL_NONE-terminated, needs room for 11 entries) and
 * returns whether robustness was requested. */
bool gpu_health_context_attribs(EGLDisplay dpy, EGLint *attribs, int cap);

/* Once the context is current: hook glGetGraphicsResetStatus and, when
 * NEOWALL_GL_DEBUG=1 or verbose, KHR_debug messages into the log. */
void gpu_health_init(bool robust, bool verbose);

/* Poll for a GPU reset. Cheap; call once per frame with the context current.
 * Returns true once the context is lost (and stays true). */
bool gpu_health_lost(void);

/* Record one swap's outcome for an output. Returns true when this output has
 * failed NW_GPU_SWAP_FAIL_LIMIT swaps in a row. */
bool gpu_health_swap_result(uint32_t *fail_streak, bool ok);

/* Pure classifier, exposed for tests: is this reset status a loss? */
bool gpu_health_status_is_lost(unsigned status);

#endif
