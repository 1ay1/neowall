/*
 * neowall/graph/graph.h — the render graph
 * ============================================================================
 *
 * A graph is N passes, each drawing into a target, each sampling M inputs, run
 * in dependency order. That is the whole model. It does not know what Shadertoy
 * is.
 *
 * This is the layer that removes the last of the engine's hardcoded lists:
 *
 *   MULTIPASS_MAX_PASSES 5      -> nw_pass_vec, any length
 *   PASS_TYPE_BUFFER_A..D       -> a pass is named; feedback is a target flag
 *   MULTIPASS_MAX_CHANNELS 4    -> nw_binding_vec, any length
 *   channel_source_t (12 arms)  -> nw_source *, any provider
 *   uniform_locations_t (~60    -> nw_uniform_vec, looked up by name
 *     hardcoded GLint fields)
 *
 * The old engine could not express "six passes" or "nine inputs" or "bind this
 * pass to a weather feed" because each of those was an enum arm or an array
 * bound. Here they are all just lengths.
 *
 * Topology is separate from GL state:
 *
 *   nw_graph        what reads what. built once per load, immutable after.
 *   nw_graph_state  targets + programs. rebuilt on resize, thrown away freely.
 *
 * That split is what makes reload cheap — rebuild the graph, diff it against
 * the old one, and keep every target whose shape did not change.
 *
 * Execution order comes from a topological sort over the binding edges, so the
 * "buffers first, then image" rule is a consequence of the dependencies rather
 * than a hardcoded pass ordering. A cycle through a non-feedback edge is a
 * build error; a self-edge is legal and means feedback.
 */
#ifndef NEOWALL_GRAPH_H
#define NEOWALL_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "neowall/gfx/gfx.h"
#include "neowall/result.h"
#include "neowall/source/source.h"
#include "neowall/vec.h"

#define NW_PASS_NAME_MAX 64

/* ==========================================================================
 * Bindings — what a pass samples
 * ========================================================================== */

typedef enum {
    /* Another pass's output. Resolved to a target at state-build time. When it
     * names the pass doing the sampling, that is feedback. */
    NW_BIND_PASS = 0,
    /* A data-plane provider: audio FFT, a live terminal, an image, anything
     * registered in the source registry. */
    NW_BIND_SOURCE,
    /* Nothing bound. Samples black rather than whatever was last in the unit,
     * which is what makes an unbound channel debuggable. */
    NW_BIND_NONE
} nw_bind_kind;

typedef struct {
    int          slot;      /* sampler unit, and the iChannelN index */
    nw_bind_kind kind;
    int          pass;      /* NW_BIND_PASS: index into graph.passes */
    nw_source   *source;    /* NW_BIND_SOURCE: borrowed, owned by the engine */
} nw_binding;

NW_VEC_DECLARE(nw_binding_vec, nw_binding)

/* ==========================================================================
 * Uniforms — scalars a pass receives
 *
 * The old engine had ~60 named GLint fields in a struct, so a new uniform
 * meant editing three files. Here a uniform is a name plus a source, and the
 * location is looked up once at compile time and cached on the pass.
 * ========================================================================== */

typedef struct {
    char       name[NW_SOURCE_NAME_MAX];
    nw_source *source;   /* borrowed */
    GLint      location; /* cached; -1 when the shader does not use it */
} nw_uniform;

NW_VEC_DECLARE(nw_uniform_vec, nw_uniform)

/* ==========================================================================
 * Passes
 * ========================================================================== */

typedef enum {
    NW_PASS_FLAG_FEEDBACK = 0x1u, /* samples its own previous frame */
    NW_PASS_FLAG_MIPMAPS  = 0x2u, /* sampled with textureLod */
    NW_PASS_FLAG_OUTPUT   = 0x4u  /* draws to the screen, not to a target */
} nw_pass_flags;

/* Fractional viewport, so a pass can occupy part of the screen. Full-screen
 * passes use {0,0,1,1}. This is what layered desktop canvases are built from:
 * a clock widget is a pass with a small rect. */
typedef struct {
    float x, y, w, h;
} nw_rect;

typedef struct {
    char            name[NW_PASS_NAME_MAX];
    char           *source;     /* assembled GLSL; owned */
    nw_binding_vec  bindings;
    nw_uniform_vec  uniforms;
    nw_rect         viewport;
    uint32_t        flags;
    float           scale;      /* resolution multiplier, 1.0 = native */
} nw_pass;

NW_VEC_DECLARE(nw_pass_vec, nw_pass)

/* ==========================================================================
 * The graph
 * ========================================================================== */

typedef struct {
    nw_pass_vec passes;
    int        *order;      /* topological execution order; owned */
    size_t      order_len;
    int         output;     /* pass index drawing to screen, -1 if none */
    char       *common;     /* GLSL prepended to every pass; owned */
} nw_graph;

void nw_graph_init(nw_graph *g);
void nw_graph_free(nw_graph *g);

/*
 * Append a pass. Returns its index, or -1 on allocation failure.
 * `name` is copied. The graph takes ownership of `source`.
 */
int nw_graph_add_pass(nw_graph *g, const char *name, char *source, uint32_t flags);

/* Look up by name. -1 when absent. Case-insensitive, so "Buffer A" and
 * "buffer a" are the same pass. */
int nw_graph_find_pass(const nw_graph *g, const char *name);

/* Bind a pass's output into another pass's sampler slot. */
nw_result nw_graph_bind_pass(nw_graph *g, int pass, int slot, int source_pass);

/* Bind a data-plane provider into a sampler slot. */
nw_result nw_graph_bind_source(nw_graph *g, int pass, int slot, nw_source *source);

/* Add a named scalar uniform fed by a provider. */
nw_result nw_graph_add_uniform(nw_graph *g, int pass, const char *name, nw_source *source);

/*
 * Compute execution order and validate.
 *
 * Fails on: a cycle through non-feedback edges, a binding naming a pass that
 * does not exist, or no output pass. Must be called before a state is built;
 * everything after this point can assume the graph is sound.
 */
nw_result nw_graph_finalize(nw_graph *g);

/* True once finalize has succeeded. */
bool nw_graph_is_finalized(const nw_graph *g);

/* ==========================================================================
 * GL state
 *
 * Split from the graph so resize and reload can throw it away without
 * touching topology.
 * ========================================================================== */

typedef struct {
    nw_gfx_program program;
    nw_gfx_target  target;      /* unused for the output pass */
    bool           has_target;
    int            width;
    int            height;
    char          *error;       /* compile failure text; owned, NULL when ok */
} nw_pass_state;

NW_VEC_DECLARE(nw_pass_state_vec, nw_pass_state)

typedef struct {
    nw_pass_state_vec passes;
    int               width;   /* the size everything was sized against */
    int               height;
    uint64_t          frame;
} nw_graph_state;

void nw_graph_state_init(nw_graph_state *s);
void nw_graph_state_free(nw_graph_state *s);

/*
 * Create targets for every non-output pass at `width` x `height` scaled by each
 * pass's `scale`. Does not compile; that is a separate step so a caller can
 * size first and compile later.
 */
nw_result nw_graph_state_build(nw_graph_state *s, const nw_graph *g, int width, int height);

/* Resize every target. Cheap when the size is unchanged. */
nw_result nw_graph_state_resize(nw_graph_state *s, const nw_graph *g, int width, int height);

/*
 * Compile every pass and cache uniform locations. Returns the first failure but
 * attempts all passes, so one broken pass does not hide the others — each
 * pass's own error lands on its state.
 */
nw_result nw_graph_state_compile(nw_graph_state *s, nw_graph *g, const char *vertex_src);

/* Per-pass compile error, or NULL. */
const char *nw_graph_state_error(const nw_graph_state *s, int pass);

/* True when every pass compiled. */
bool nw_graph_state_is_ready(const nw_graph_state *s, const nw_graph *g);

/* The target a pass draws into, or NULL for the output pass. */
nw_gfx_target *nw_graph_state_target(nw_graph_state *s, int pass);

/*
 * Resolve what a binding should sample, as a GL texture name.
 *
 * NW_BIND_PASS resolves through nw_gfx_target_read(), so a feedback pass gets
 * last frame's texture and never the one being written. NW_BIND_SOURCE asks
 * the provider. NW_BIND_NONE and anything unresolvable give 0, which samples
 * black rather than stale contents.
 */
unsigned nw_graph_state_resolve_binding(nw_graph_state *s, const nw_graph *g, int pass,
                                        int slot);

/* Pixel size a pass renders at, after its scale is applied. */
void nw_graph_pass_size(const nw_graph *g, int pass, int base_w, int base_h, int *out_w,
                        int *out_h);

#endif /* NEOWALL_GRAPH_H */
