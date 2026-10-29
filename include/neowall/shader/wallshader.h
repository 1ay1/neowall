/* neowall/shader/wallshader.h — one output's running shader.
 *
 * Ties the parts together the way an output uses them: the parsed source,
 * the GL engine, the manifest's live `uniforms` bindings, the reactive
 * uniforms, iState persistence, and an optional terminal. render.c hands it
 * the frame's time, size and windows; everything Shadertoy-shaped stays in
 * the engine. Loop thread, with the output's GL context current. */
#ifndef NEOWALL_WALLSHADER_H
#define NEOWALL_WALLSHADER_H

#include <stdbool.h>
#include <stdint.h>

#include "neowall/result.h"
#include "neowall/shader/engine.h"
#include "neowall/shader/term_gpu.h"

#define NW_SHADER_MAX_WINDOWS 16

typedef struct wallshader wallshader;

/* Load a .glsl or .neowall and compile it. On failure nothing is returned,
 * the errors are logged and kept for wallshader_last_errors(). */
nw_result wallshader_load(const char *path, wallshader **out);
/* The same from text in memory (the terminal's built-in pass-through). */
nw_result wallshader_load_text(const char *text, const char *name, wallshader **out);
void wallshader_free(wallshader *s);

/* The same without blocking the loop: the driver compiles on its own threads
 * (KHR_parallel_shader_compile) while the caller keeps drawing the old
 * shader. Errors in the files show up right away; compile errors on the
 * poll that returns FAILED. A program-cache hit is DONE on the first poll.
 *
 *   wallshader_load_start(path, &p)   parse, issue the compiles
 *   wallshader_load_poll(p, &s)       PENDING / DONE (s is yours) / FAILED
 *   wallshader_load_cancel(p)         always, once; safe after DONE/FAILED */
typedef struct wallshader_pending wallshader_pending;
nw_result wallshader_load_start(const char *path, wallshader_pending **out);
nw_build_state wallshader_load_poll(wallshader_pending *p, wallshader **out);
void wallshader_load_cancel(wallshader_pending *p);

/* "file:line: message" with the source line and a caret, from the last
 * failed load. Valid until the next load. */
const char *wallshader_last_errors(void);

/* Hand it a terminal to show (iTermCells and friends). Takes ownership. */
void wallshader_attach_terminal(wallshader *s, term_gpu *t);
term_gpu *wallshader_terminal(wallshader *s);

typedef struct {
    int width, height;
    double time;                 /* iTime, seconds */
    double present_at;           /* when this frame is shown (s, monotonic), or 0 */
    float mouse[2];
    int span_w, span_h;          /* the whole span, or 0x0 */
    int span_x, span_y;          /* this output's offset in it (y from the bottom) */
    const float *windows;        /* count rects, x y w h, output px, y-down */
    int window_count;
    const float *focused;        /* 4 floats, or NULL */
    /* the config's `channels` (and image cycling on iChannel0): textures
     * that fill channels the shader's own bindings leave open, or 0 */
    unsigned channels[4];
    int channel_w[4], channel_h[4];
} wallshader_frame;

/* Draw into the bound framebuffer. */
void wallshader_draw(wallshader *s, const wallshader_frame *f);

/* Whether the output is seen. A hidden shader's manifest sources stop
 * polling, spawning and sampling until it shows again. No GL. */
void wallshader_set_visible(wallshader *s, bool visible);

/* Whether it changes over time (else one frame is enough). */
bool wallshader_animated(const wallshader *s);
int wallshader_pass_count(const wallshader *s);

#endif /* NEOWALL_WALLSHADER_H */
