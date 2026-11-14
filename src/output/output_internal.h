/* Shared by the files of the output module (src/output/). Not public.
 *
 *   output.c              create, destroy, refcount, EGL surface, geometry
 *   apply.c               apply a config; staged reloads
 *   cycle.c               timed and requested cycling
 *   span_group.c          one scene across several monitors
 *   preload.c             decode the next image on a worker thread
 *   pace.c                vsync, frame timer, phase-locked pacing
 *   wallpaper_image.c     show an image
 *   wallpaper_shader.c    show a shader, background builds
 *   wallpaper_terminal.c  show a live terminal */
#ifndef NEOWALL_OUTPUT_INTERNAL_H
#define NEOWALL_OUTPUT_INTERNAL_H

#include "neowall/output/output.h"

struct wallpaper_config;

/* connector name if known ("DP-1"), else the model */
const char *output_get_identifier(const struct output_state *output);
/* keyboard interactivity follows the wallpaper type (terminal wants it) */
void output_sync_keyboard_focus(struct output_state *output);
/* swap interval and frame timer from the config */
void output_configure_vsync(struct output_state *output);
bool output_configure_frame_timer(struct output_state *output);
/* a staged reload goes live / is dropped (apply.c) */
void output_commit_staged(struct output_state *output, struct wallpaper_config *cfg);
void output_unstage(struct output_state *output);

#endif /* NEOWALL_OUTPUT_INTERNAL_H */
