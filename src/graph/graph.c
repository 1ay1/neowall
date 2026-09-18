/*
 * graph.c — render graph construction, ordering, and GL state.
 *
 * See include/neowall/graph/graph.h for the model and
 * docs/architecture/SHADER_ENGINE_V2.md for why it replaces the old
 * fixed-array multipass_shader_t.
 *
 * The only subtle part is the topological sort. Two rules:
 *
 *   - A self-edge is feedback, not a cycle. A pass sampling its own previous
 *     frame is the entire basis of accumulation buffers, and gfx resolves it
 *     through the ping-pong pair so there is no ordering problem to solve.
 *   - Any other cycle is a build error. A reads B reads A within one frame is
 *     unsatisfiable, and catching it here means the render path never has to.
 */

#include "neowall/graph/graph.h"
#include "neowall/shader/shader_log.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

NW_VEC_DEFINE(nw_binding_vec, nw_binding)
NW_VEC_DEFINE(nw_uniform_vec, nw_uniform)
NW_VEC_DEFINE(nw_pass_vec, nw_pass)
NW_VEC_DEFINE(nw_pass_state_vec, nw_pass_state)

/* ==========================================================================
 * Graph construction
 * ========================================================================== */

void nw_graph_init(nw_graph *g) {
    if (!g) {
        return;
    }
    memset(g, 0, sizeof(*g));
    nw_pass_vec_init(&g->passes);
    g->output = -1;
}

void nw_graph_free(nw_graph *g) {
    if (!g) {
        return;
    }
    for (size_t i = 0; i < g->passes.len; i++) {
        nw_pass *p = &g->passes.data[i];
        free(p->source);
        nw_binding_vec_free(&p->bindings);
        nw_uniform_vec_free(&p->uniforms);
    }
    nw_pass_vec_free(&g->passes);
    free(g->order);
    free(g->common);
    memset(g, 0, sizeof(*g));
    g->output = -1;
}

int nw_graph_add_pass(nw_graph *g, const char *name, char *source, uint32_t flags) {
    if (!g || !name) {
        return -1;
    }

    nw_pass p;
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "%s", name);
    p.source   = source;
    p.flags    = flags;
    p.viewport = (nw_rect){0.0f, 0.0f, 1.0f, 1.0f};
    p.scale    = 1.0f;
    nw_binding_vec_init(&p.bindings);
    nw_uniform_vec_init(&p.uniforms);

    if (!nw_pass_vec_push(&g->passes, p)) {
        nw_binding_vec_free(&p.bindings);
        nw_uniform_vec_free(&p.uniforms);
        return -1;
    }

    int index = (int)g->passes.len - 1;
    if (flags & NW_PASS_FLAG_OUTPUT) {
        g->output = index;
    }
    /* Any structural change invalidates the order. */
    free(g->order);
    g->order     = NULL;
    g->order_len = 0;
    return index;
}

int nw_graph_find_pass(const nw_graph *g, const char *name) {
    if (!g || !name) {
        return -1;
    }
    for (size_t i = 0; i < g->passes.len; i++) {
        if (strcasecmp(g->passes.data[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* Replace an existing binding in the same slot rather than stacking two, so a
 * manifest overriding a frontend's guess wins instead of racing it. */
static nw_result bind_slot(nw_graph *g, int pass, nw_binding b) {
    if (!g || pass < 0 || (size_t)pass >= g->passes.len) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: bind to nonexistent pass");
    }
    if (b.slot < 0) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: negative sampler slot");
    }

    nw_pass *p = &g->passes.data[pass];
    for (size_t i = 0; i < p->bindings.len; i++) {
        if (p->bindings.data[i].slot == b.slot) {
            p->bindings.data[i] = b;
            return nw_ok();
        }
    }
    if (!nw_binding_vec_push(&p->bindings, b)) {
        return nw_err(NW_ERR_OOM, "graph: out of memory adding binding");
    }

    free(g->order);
    g->order     = NULL;
    g->order_len = 0;
    return nw_ok();
}

nw_result nw_graph_bind_pass(nw_graph *g, int pass, int slot, int source_pass) {
    if (!g || source_pass < 0 || (size_t)source_pass >= g->passes.len) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: bind from nonexistent pass");
    }
    nw_binding b = {.slot = slot, .kind = NW_BIND_PASS, .pass = source_pass, .source = NULL};

    nw_result r = bind_slot(g, pass, b);
    if (nw_is_err(r)) {
        return r;
    }
    /* A pass sampling itself is feedback, which the target must double-buffer.
     * Recording it here means the frontend does not have to remember to. */
    if (pass == source_pass) {
        g->passes.data[pass].flags |= NW_PASS_FLAG_FEEDBACK;
    }
    return nw_ok();
}

nw_result nw_graph_bind_source(nw_graph *g, int pass, int slot, nw_source *source) {
    if (!source) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: null source binding");
    }
    nw_binding b = {.slot = slot, .kind = NW_BIND_SOURCE, .pass = -1, .source = source};
    return bind_slot(g, pass, b);
}

nw_result nw_graph_add_uniform(nw_graph *g, int pass, const char *name, nw_source *source) {
    if (!g || pass < 0 || (size_t)pass >= g->passes.len) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: uniform on nonexistent pass");
    }
    if (!name || !name[0] || !source) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: uniform needs a name and a source");
    }

    nw_pass *p = &g->passes.data[pass];

    /* Same-name replaces, so a later declaration overrides an earlier one
     * instead of shadowing it unpredictably at lookup time. */
    for (size_t i = 0; i < p->uniforms.len; i++) {
        if (strcmp(p->uniforms.data[i].name, name) == 0) {
            p->uniforms.data[i].source   = source;
            p->uniforms.data[i].location = -1;
            return nw_ok();
        }
    }

    nw_uniform u;
    memset(&u, 0, sizeof(u));
    snprintf(u.name, sizeof(u.name), "%s", name);
    u.source   = source;
    u.location = -1;

    if (!nw_uniform_vec_push(&p->uniforms, u)) {
        return nw_err(NW_ERR_OOM, "graph: out of memory adding uniform");
    }
    return nw_ok();
}

/* ==========================================================================
 * Topological ordering
 * ========================================================================== */

/* Kahn's algorithm over the pass-to-pass edges. Self-edges are skipped: they
 * are feedback, resolved through the ping-pong pair, so they impose no ordering
 * constraint within a frame. */
nw_result nw_graph_finalize(nw_graph *g) {
    if (!g) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: null graph");
    }
    size_t n = g->passes.len;
    if (n == 0) {
        return nw_err(NW_ERR_PARSE, "graph: no passes");
    }
    if (g->output < 0 || (size_t)g->output >= n) {
        return nw_err(NW_ERR_PARSE, "graph: no output pass");
    }

    /* Validate every edge before ordering, so a bad index cannot be read as a
     * dependency below. */
    for (size_t i = 0; i < n; i++) {
        const nw_pass *p = &g->passes.data[i];
        for (size_t b = 0; b < p->bindings.len; b++) {
            const nw_binding *bind = &p->bindings.data[b];
            if (bind->kind == NW_BIND_PASS) {
                if (bind->pass < 0 || (size_t)bind->pass >= n) {
                    return nw_err(NW_ERR_PARSE, "graph: binding names a nonexistent pass");
                }
            } else if (bind->kind == NW_BIND_SOURCE && !bind->source) {
                return nw_err(NW_ERR_PARSE, "graph: source binding with no provider");
            }
        }
    }

    int *in_degree = calloc(n, sizeof(int));
    int *order     = calloc(n, sizeof(int));
    int *queue     = calloc(n, sizeof(int));
    if (!in_degree || !order || !queue) {
        free(in_degree);
        free(order);
        free(queue);
        return nw_err(NW_ERR_OOM, "graph: out of memory ordering passes");
    }

    for (size_t i = 0; i < n; i++) {
        const nw_pass *p = &g->passes.data[i];
        for (size_t b = 0; b < p->bindings.len; b++) {
            const nw_binding *bind = &p->bindings.data[b];
            if (bind->kind != NW_BIND_PASS) {
                continue;
            }
            if ((size_t)bind->pass == i) {
                continue; /* feedback */
            }
            in_degree[i]++;
        }
    }

    size_t head = 0, tail = 0;
    for (size_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            queue[tail++] = (int)i;
        }
    }

    size_t placed = 0;
    while (head < tail) {
        int current      = queue[head++];
        order[placed++]  = current;

        /* Anything sampling `current` just lost a dependency. */
        for (size_t i = 0; i < n; i++) {
            if ((int)i == current) {
                continue;
            }
            const nw_pass *p = &g->passes.data[i];
            for (size_t b = 0; b < p->bindings.len; b++) {
                const nw_binding *bind = &p->bindings.data[b];
                if (bind->kind == NW_BIND_PASS && bind->pass == current &&
                    (size_t)bind->pass != i) {
                    if (--in_degree[i] == 0) {
                        queue[tail++] = (int)i;
                    }
                }
            }
        }
    }

    free(in_degree);
    free(queue);

    if (placed != n) {
        /* Some pass never reached in-degree zero: a real cycle. Refusing here
         * is much kinder than rendering something subtly wrong forever. */
        free(order);
        return nw_err(NW_ERR_PARSE, "graph: dependency cycle between passes");
    }

    free(g->order);
    g->order     = order;
    g->order_len = n;
    return nw_ok();
}

bool nw_graph_is_finalized(const nw_graph *g) {
    return g && g->order && g->order_len == g->passes.len && g->passes.len > 0;
}

void nw_graph_pass_size(const nw_graph *g, int pass, int base_w, int base_h, int *out_w,
                        int *out_h) {
    float scale = 1.0f;
    if (g && pass >= 0 && (size_t)pass < g->passes.len) {
        scale = g->passes.data[pass].scale;
        if (scale <= 0.0f) {
            scale = 1.0f;
        }
    }
    int w = (int)((float)base_w * scale);
    int h = (int)((float)base_h * scale);
    /* Never hand GL a zero-sized target; a heavily downscaled pass on a small
     * output can round to nothing. */
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

/* ==========================================================================
 * GL state
 * ========================================================================== */

void nw_graph_state_init(nw_graph_state *s) {
    if (!s) {
        return;
    }
    memset(s, 0, sizeof(*s));
    nw_pass_state_vec_init(&s->passes);
}

void nw_graph_state_free(nw_graph_state *s) {
    if (!s) {
        return;
    }
    for (size_t i = 0; i < s->passes.len; i++) {
        nw_pass_state *ps = &s->passes.data[i];
        nw_gfx_program_destroy(&ps->program);
        nw_gfx_target_destroy(&ps->target);
        free(ps->error);
    }
    nw_pass_state_vec_free(&s->passes);
    memset(s, 0, sizeof(*s));
}

nw_result nw_graph_state_build(nw_graph_state *s, const nw_graph *g, int width, int height) {
    if (!s || !g) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: null state build args");
    }
    if (!nw_graph_is_finalized(g)) {
        return nw_err(NW_ERR_STATE, "graph: build before finalize");
    }
    if (width <= 0 || height <= 0) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: build needs positive dimensions");
    }

    nw_graph_state_free(s);
    nw_graph_state_init(s);

    for (size_t i = 0; i < g->passes.len; i++) {
        const nw_pass *p = &g->passes.data[i];

        nw_pass_state ps;
        memset(&ps, 0, sizeof(ps));

        int pw, ph;
        nw_graph_pass_size(g, (int)i, width, height, &pw, &ph);
        ps.width  = pw;
        ps.height = ph;

        /* The output pass draws to the screen, so it needs no target. */
        if (!(p->flags & NW_PASS_FLAG_OUTPUT)) {
            nw_gfx_filter filter =
                (p->flags & NW_PASS_FLAG_MIPMAPS) ? NW_GFX_FILTER_MIPMAP : NW_GFX_FILTER_LINEAR;
            bool feedback = (p->flags & NW_PASS_FLAG_FEEDBACK) != 0;

            /* 16F: buffer passes are memory-bound, and half floats give the
             * precision accumulation needs at half the bandwidth of 32F. */
            nw_result r = nw_gfx_target_create(&ps.target, pw, ph, NW_GFX_FMT_RGBA16F, filter,
                                               feedback);
            if (nw_is_err(r)) {
                nw_graph_state_free(s);
                return r;
            }
            ps.has_target = true;
        }

        if (!nw_pass_state_vec_push(&s->passes, ps)) {
            nw_gfx_target_destroy(&ps.target);
            nw_graph_state_free(s);
            return nw_err(NW_ERR_OOM, "graph: out of memory building state");
        }
    }

    s->width  = width;
    s->height = height;
    return nw_ok();
}

nw_result nw_graph_state_resize(nw_graph_state *s, const nw_graph *g, int width, int height) {
    if (!s || !g) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: null resize args");
    }
    if (width <= 0 || height <= 0) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: resize needs positive dimensions");
    }
    if (s->width == width && s->height == height) {
        return nw_ok();
    }
    if (s->passes.len != g->passes.len) {
        return nw_err(NW_ERR_STATE, "graph: state does not match graph");
    }

    for (size_t i = 0; i < s->passes.len; i++) {
        nw_pass_state *ps = &s->passes.data[i];

        int pw, ph;
        nw_graph_pass_size(g, (int)i, width, height, &pw, &ph);
        ps->width  = pw;
        ps->height = ph;

        if (ps->has_target) {
            nw_result r = nw_gfx_target_resize(&ps->target, pw, ph);
            if (nw_is_err(r)) {
                return r;
            }
        }
    }

    s->width  = width;
    s->height = height;
    return nw_ok();
}

nw_result nw_graph_state_compile(nw_graph_state *s, nw_graph *g, const char *vertex_src) {
    if (!s || !g || !vertex_src) {
        return nw_err(NW_ERR_INVALID_ARG, "graph: null compile args");
    }
    if (s->passes.len != g->passes.len) {
        return nw_err(NW_ERR_STATE, "graph: state does not match graph");
    }

    nw_result first_error = nw_ok();

    for (size_t i = 0; i < g->passes.len; i++) {
        nw_pass       *p  = &g->passes.data[i];
        nw_pass_state *ps = &s->passes.data[i];

        free(ps->error);
        ps->error = NULL;
        nw_gfx_program_destroy(&ps->program);

        if (!p->source) {
            ps->error = strdup("pass has no source");
            if (nw_is_ok(first_error)) {
                first_error = nw_err(NW_ERR_PARSE, "graph: pass has no source");
            }
            continue;
        }

        nw_result r = nw_gfx_program_build(&ps->program, vertex_src, p->source);
        if (nw_is_err(r)) {
            /* Keep going. Every pass gets its own error so a shader with two
             * broken passes reports both, instead of the last one winning a
             * shared buffer. */
            ps->error = strdup(ps->program.log[0] ? ps->program.log : "compile failed");
            log_error("Pass '%s' failed to compile: %s", p->name,
                      ps->error ? ps->error : "(no detail)");
            if (nw_is_ok(first_error)) {
                first_error = r;
            }
            continue;
        }

        /* Cache uniform locations now. -1 means the shader does not reference
         * it, which is normal and not an error: unused uniforms are stripped by
         * the GLSL compiler. */
        for (size_t u = 0; u < p->uniforms.len; u++) {
            p->uniforms.data[u].location =
                nw_gfx_program_uniform(&ps->program, p->uniforms.data[u].name);
        }
    }

    return first_error;
}

const char *nw_graph_state_error(const nw_graph_state *s, int pass) {
    if (!s || pass < 0 || (size_t)pass >= s->passes.len) {
        return NULL;
    }
    return s->passes.data[pass].error;
}

bool nw_graph_state_is_ready(const nw_graph_state *s, const nw_graph *g) {
    if (!s || !g || s->passes.len != g->passes.len || s->passes.len == 0) {
        return false;
    }
    for (size_t i = 0; i < s->passes.len; i++) {
        if (s->passes.data[i].program.id == 0) {
            return false;
        }
    }
    return true;
}

nw_gfx_target *nw_graph_state_target(nw_graph_state *s, int pass) {
    if (!s || pass < 0 || (size_t)pass >= s->passes.len) {
        return NULL;
    }
    nw_pass_state *ps = &s->passes.data[pass];
    return ps->has_target ? &ps->target : NULL;
}

unsigned nw_graph_state_resolve_binding(nw_graph_state *s, const nw_graph *g, int pass,
                                        int slot) {
    if (!s || !g || pass < 0 || (size_t)pass >= g->passes.len) {
        return 0;
    }

    const nw_pass *p = &g->passes.data[pass];
    for (size_t i = 0; i < p->bindings.len; i++) {
        const nw_binding *b = &p->bindings.data[i];
        if (b->slot != slot) {
            continue;
        }

        switch (b->kind) {
            case NW_BIND_PASS: {
                nw_gfx_target *t = nw_graph_state_target(s, b->pass);
                if (!t) {
                    return 0;
                }
                /* read() is what keeps a feedback pass off the texture it is
                 * writing this frame. */
                const nw_gfx_texture *tex = nw_gfx_target_read(t);
                return tex ? tex->id : 0;
            }
            case NW_BIND_SOURCE:
                return nw_source_texture(b->source);
            case NW_BIND_NONE:
            default:
                return 0;
        }
    }
    /* Nothing bound to this slot: sample black, not whatever was last in the
     * unit. An unbound channel showing stale contents is very hard to debug. */
    return 0;
}
