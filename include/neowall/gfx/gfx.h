/*
 * neowall/gfx/gfx.h — GL objects, and nothing else
 * ============================================================================
 *
 * The bottom layer of the v2 shader engine (see
 * docs/architecture/SHADER_ENGINE_V2.md). Programs, textures, render targets.
 * It knows about OpenGL and knows nothing about Shadertoy, passes, channels,
 * multipass, reactive data, or wallpapers.
 *
 * Today that logic lives inline in shader_multipass.c, tangled with pass
 * semantics: the FBO creation loop is inside a `pass->type >= PASS_TYPE_BUFFER_A`
 * branch, and the ping-pong pair is two raw GLuints plus an index that callers
 * flip by hand. So the same five glTexParameteri calls appear in three places
 * and the "which texture am I writing this frame" question is answered
 * differently depending on who is asking.
 *
 * Pulling it out gives the graph layer something to build on that can be
 * reasoned about on its own:
 *
 *   nw_gfx_program   a compiled+linked program, with the error log kept
 *   nw_gfx_texture   a texture with a known format and size
 *   nw_gfx_target    a render target; optionally double-buffered for feedback
 *
 * Feedback is the one piece of real behaviour here. A pass that reads its own
 * previous frame needs two textures and a flip; that is a property of the
 * target, not of the pass, so nw_gfx_target owns it and exposes exactly two
 * accessors — read() and write(). No caller indexes an array.
 *
 * Every function is safe on a zeroed struct, so partially-constructed objects
 * clean up correctly on an error path.
 */
#ifndef NEOWALL_GFX_H
#define NEOWALL_GFX_H

#include <stdbool.h>
#include <stddef.h>

#include "neowall/result.h"
#include "neowall/shader/platform_compat.h"

/* ==========================================================================
 * Programs
 * ========================================================================== */

/* Longest compile/link log retained. Driver logs can run long; the head is
 * where the first error is, and that is what gets shown. */
#define NW_GFX_LOG_MAX 4096

typedef struct {
    GLuint id;                    /* 0 when not built */
    char   log[NW_GFX_LOG_MAX];   /* compile or link diagnostics, "" on success */
} nw_gfx_program;

/*
 * Compile and link. On failure `out->id` stays 0, `out->log` holds the driver
 * diagnostics, and the result carries the stage that failed.
 *
 * The log is kept on the object rather than in a global, because with N passes
 * a shared "last error" buffer means pass 3's failure overwrites pass 1's
 * before anyone reads it.
 */
nw_result nw_gfx_program_build(nw_gfx_program *out, const char *vertex_src,
                               const char *fragment_src);

/* Delete and zero. Safe on a zeroed or already-destroyed program. */
void nw_gfx_program_destroy(nw_gfx_program *prog);

/* glGetUniformLocation, or -1. Safe when the program failed to build. */
GLint nw_gfx_program_uniform(const nw_gfx_program *prog, const char *name);

/* ==========================================================================
 * Textures
 * ========================================================================== */

typedef enum {
    NW_GFX_FMT_RGBA8 = 0, /* 8-bit unorm; inputs, glyph atlases, images */
    NW_GFX_FMT_RGBA16F,   /* half float; buffer passes — precision at half the
                           * bandwidth of 32F, which matters because these are
                           * memory-bound */
    NW_GFX_FMT_RGBA32F    /* full float; when 16F banding shows */
} nw_gfx_format;

typedef enum {
    NW_GFX_FILTER_NEAREST = 0,
    NW_GFX_FILTER_LINEAR,
    NW_GFX_FILTER_MIPMAP /* linear + mipmap chain, for textureLod sampling */
} nw_gfx_filter;

typedef enum {
    NW_GFX_WRAP_CLAMP = 0,
    NW_GFX_WRAP_REPEAT
} nw_gfx_wrap;

typedef struct {
    GLuint        id;
    int           width;
    int           height;
    nw_gfx_format format;
    nw_gfx_filter filter;
    nw_gfx_wrap   wrap;
} nw_gfx_texture;

/* Allocate storage. `pixels` may be NULL for an uninitialised target. */
nw_result nw_gfx_texture_create(nw_gfx_texture *out, int width, int height,
                                nw_gfx_format format, nw_gfx_filter filter,
                                nw_gfx_wrap wrap, const void *pixels);

/* Reallocate at a new size. No-op when the size already matches, so resize
 * paths can call it unconditionally. */
nw_result nw_gfx_texture_resize(nw_gfx_texture *tex, int width, int height);

/* Change filtering in place. Used when a shader turns out to sample a buffer
 * with textureLod and the mipmap chain has to appear after the fact. */
void nw_gfx_texture_set_filter(nw_gfx_texture *tex, nw_gfx_filter filter);

/* Regenerate mipmaps. No-op unless the filter is NW_GFX_FILTER_MIPMAP. */
void nw_gfx_texture_gen_mipmaps(nw_gfx_texture *tex);

/* Delete and zero. Safe on a zeroed texture. */
void nw_gfx_texture_destroy(nw_gfx_texture *tex);

/* Bind to a sampler unit. Binding id 0 is valid and unbinds. */
void nw_gfx_texture_bind(const nw_gfx_texture *tex, int unit);

/* ==========================================================================
 * Render targets
 * ========================================================================== */

/*
 * An FBO plus its colour attachment. When `feedback` is set there are two
 * textures and the target flips between them, so a pass can sample last
 * frame's result while writing this frame's.
 *
 * Callers never see the pair. They ask for read() or write() and the target
 * resolves which is which.
 */
typedef struct {
    GLuint         fbo;
    nw_gfx_texture textures[2];
    bool           feedback;    /* double-buffered */
    int            front;       /* index currently being written */
    bool           needs_clear; /* set on create/resize; cleared on first bind */
} nw_gfx_target;

/* Create an FBO and its attachment(s). `feedback` allocates the second
 * texture and enables flipping. */
nw_result nw_gfx_target_create(nw_gfx_target *out, int width, int height,
                               nw_gfx_format format, nw_gfx_filter filter,
                               bool feedback);

/* Resize every attachment. No-op when the size already matches. Contents are
 * not preserved, so the target is marked for clearing. */
nw_result nw_gfx_target_resize(nw_gfx_target *target, int width, int height);

/* Delete and zero. Safe on a zeroed target. */
void nw_gfx_target_destroy(nw_gfx_target *target);

/* Bind for drawing and set the viewport to the full target. Clears first if
 * the target was just created or resized. */
void nw_gfx_target_bind(nw_gfx_target *target);

/* Bind the default framebuffer (the screen). */
void nw_gfx_target_unbind(void);

/* Flip the buffers. Call once after drawing a feedback pass; a no-op on a
 * single-buffered target, so the caller need not special-case it. */
void nw_gfx_target_swap(nw_gfx_target *target);

/* The texture to SAMPLE: last frame's result on a feedback target, and the
 * only texture otherwise. NULL when the target is not created. */
const nw_gfx_texture *nw_gfx_target_read(const nw_gfx_target *target);

/* The texture being WRITTEN this frame. */
const nw_gfx_texture *nw_gfx_target_write(const nw_gfx_target *target);

/* Mark for clearing on next bind, e.g. after a shader reload makes stale
 * feedback contents meaningless. */
void nw_gfx_target_mark_clear(nw_gfx_target *target);

/* ==========================================================================
 * Misc
 * ========================================================================== */

/* Drain the GL error queue, logging each. Returns true if any were found.
 * `where` tags the log line. */
bool nw_gfx_check_errors(const char *where);

/* Human-readable format name, for logs and diagnostics. */
const char *nw_gfx_format_name(nw_gfx_format format);

/* Bytes per pixel, for VRAM accounting. */
size_t nw_gfx_format_bpp(nw_gfx_format format);

#endif /* NEOWALL_GFX_H */
