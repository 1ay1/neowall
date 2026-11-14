#ifndef X11_OCCLUSION_H
#define X11_OCCLUSION_H

/* Fullscreen detection on X11 (EWMH), per output. The X11 backend owns one
 * of these; everything here runs on the loop thread, which owns the
 * Display. */

#include <X11/Xlib.h>
#include <stdbool.h>
#include <stdint.h>

struct neowall_state;

typedef struct x11_occlusion {
    Display *dpy;
    Window root;
    struct neowall_state *state;
    Atom client_list_stacking, wm_state, wm_state_fullscreen;
    uint64_t last_check_ms;     /* polled, at most every 250 ms */
    bool active;
} x11_occlusion;

/* Intern the atoms and take a first look. */
bool x11_occlusion_init(x11_occlusion *occ, Display *display, struct neowall_state *state);
/* Re-check if it's been long enough; sets each output's `occluded`. */
void x11_occlusion_update(x11_occlusion *occ);
void x11_occlusion_cleanup(x11_occlusion *occ);

#endif /* X11_OCCLUSION_H */
