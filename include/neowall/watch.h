/* Shader hot-reload watcher.
 *
 * `neowall watch foo.glsl` recompiles the shader on every save and prints the
 * error inline, keeping the last good version on screen when compilation
 * fails. Without it the authoring loop is edit -> reload -> squint at the log,
 * and a typo blanks the wallpaper instead of telling you what is wrong.
 *
 * The watch fd joins the main poll(2) set, so this costs nothing when idle and
 * needs no extra thread.
 *
 * Editors rarely just write a file: most save by writing a temp file and
 * renaming over the target, which destroys the inode an IN_MODIFY watch is
 * attached to. So the watch is placed on the parent DIRECTORY and filtered by
 * name, which catches write, rename-into-place and atomic-replace alike. */

#ifndef NEOWALL_WATCH_H
#define NEOWALL_WATCH_H

#include <stdbool.h>

struct neowall_state;

/* Begin watching `path` (a .glsl or .neowall file). Returns an fd to add to the
 * poll set, or -1 if watching is unavailable. Safe to call once at startup. */
int watch_init(const char *path);

/* Drain the inotify fd and report whether the watched file changed.
 *
 * Must be called when the watch fd reports POLLIN, and always drains the fd
 * fully so poll() does not spin. Coalesces the burst of events a single save
 * produces into one true. */
bool watch_consume(int fd);

/* Reapply the watched shader to every output, reporting compile status to
 * stdout. On failure the previous shader keeps rendering. */
void watch_reload(struct neowall_state *state, const char *path);

/* Release watch resources. */
void watch_cleanup(int fd);

#endif /* NEOWALL_WATCH_H */
