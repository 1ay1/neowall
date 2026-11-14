#ifndef X11_OCCLUSION_H
#define X11_OCCLUSION_H

/* X11-side occlusion detection (EWMH + XRandR).
 * Internal to the X11 backend; not part of the public abstraction. */

#include <stdbool.h>

typedef struct _XDisplay Display;
struct neowall_state;

bool x11_occlusion_init(Display *display, struct neowall_state *state);
void x11_occlusion_update(struct neowall_state *state);
void x11_occlusion_cleanup(void);

#endif /* X11_OCCLUSION_H */
