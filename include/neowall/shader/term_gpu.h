/* neowall/shader/term_gpu.h — a live terminal as shader inputs.
 *
 * Owns the terminal (PTY child, grid, glyph atlas) and the textures a
 * terminal wallpaper samples: iTermCells, iTermChange, iTermAtlas,
 * iTermColorAtlas, plus the iTerm* uniforms. Each frame it uploads what
 * changed and hands the engine name/value lists, so the shader engine knows
 * nothing about terminals. Needs a current GL context except where noted. */
#ifndef NEOWALL_TERM_GPU_H
#define NEOWALL_TERM_GPU_H

#include <stdbool.h>
#include <stddef.h>

#include "neowall/result.h"
#include "neowall/shader/engine.h"

typedef struct term_gpu term_gpu;

typedef struct {
    const char *cmd;
    int cols, rows, cell_w, cell_h;
    const char *font_path, *font_bold, *font_italic;
    const char *cwd, *term_env;
    long fg, bg;                    /* 0xRRGGBB, or -1 for the built-in */
    void (*on_output)(void *ctx);   /* reader thread: the child drew */
    void *on_output_ctx;
} term_gpu_opts;

/* Spawn the child and create the textures. */
nw_result term_gpu_new(const term_gpu_opts *o, term_gpu **out);
/* Kill the child and delete the textures. */
void term_gpu_free(term_gpu *t);
/* Kill the child only, no GL. For shutdown, where no context is current.
 * Idempotent; term_gpu_free still has to run (or the process exits). */
void term_gpu_shutdown(term_gpu *t);

/* Upload whatever changed and fill the lists the engine reads. `time` is the
 * frame's iTime (the cursor slide is timed in it). The lists point into `t`
 * and stay valid until the next call. */
void term_gpu_frame(term_gpu *t, double time, const nw_uniform_value **unis, size_t *n_unis,
                    const nw_texture_value **texs, size_t *n_texs);

bool term_gpu_resize(term_gpu *t, int cols, int rows);
bool term_gpu_mouse(term_gpu *t, int px, int py, int button, bool pressed, bool motion);
bool term_gpu_write(term_gpu *t, const void *bytes, size_t len);
/* No GL. */
bool term_gpu_animating(const term_gpu *t, unsigned fx_settle_ms);
bool term_gpu_child_exited(const term_gpu *t);
void term_gpu_set_fx(term_gpu *t, const float fx[4], float fade);

/* The band of the surface the last frame's cell changes cover, in EGL
 * damage coordinates (bottom-left origin). Only meaningful for the crisp
 * single-pass pass-through, where a cell row maps to a pixel band. false
 * means damage everything. */
bool term_gpu_damage(const term_gpu *t, int surf_w, int surf_h, int *x, int *y, int *w, int *h);

#endif /* NEOWALL_TERM_GPU_H */
