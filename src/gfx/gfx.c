/*
 * gfx.c — GL object handling, extracted from shader_multipass.c.
 *
 * See include/neowall/gfx/gfx.h for the contract and
 * docs/architecture/SHADER_ENGINE_V2.md for where this sits.
 *
 * Everything here is mechanical GL. The only real behaviour is the feedback
 * flip in nw_gfx_target, which exists so no caller has to keep track of which
 * half of a ping-pong pair it is allowed to read this frame.
 */

#include "neowall/gfx/gfx.h"
#include "neowall/shader/shader_log.h"

#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Format mapping
 * ========================================================================== */

typedef struct {
    GLint  internal_format;
    GLenum format;
    GLenum type;
    size_t bpp;
    const char *name;
} format_info;

static const format_info g_formats[] = {
    [NW_GFX_FMT_RGBA8]   = {GL_RGBA8,   GL_RGBA, GL_UNSIGNED_BYTE, 4,  "RGBA8"},
    [NW_GFX_FMT_RGBA16F] = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT,    8,  "RGBA16F"},
    [NW_GFX_FMT_RGBA32F] = {GL_RGBA32F, GL_RGBA, GL_FLOAT,         16, "RGBA32F"},
};

static const format_info *format_lookup(nw_gfx_format f) {
    if (f < NW_GFX_FMT_RGBA8 || f > NW_GFX_FMT_RGBA32F) {
        return &g_formats[NW_GFX_FMT_RGBA8];
    }
    return &g_formats[f];
}

const char *nw_gfx_format_name(nw_gfx_format format) { return format_lookup(format)->name; }
size_t      nw_gfx_format_bpp(nw_gfx_format format) { return format_lookup(format)->bpp; }

/* ==========================================================================
 * Programs
 * ========================================================================== */

/* Compile one stage. On failure the driver log is copied into `log`, which is
 * what the caller surfaces to the user. */
static GLuint compile_stage(GLenum type, const char *source, char *log, size_t log_cap) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        snprintf(log, log_cap, "glCreateShader failed");
        return 0;
    }

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            GLsizei written = 0;
            glGetShaderInfoLog(shader, (GLsizei)log_cap - 1, &written, log);
            log[written < (GLsizei)log_cap ? written : (GLsizei)log_cap - 1] = '\0';
        } else {
            snprintf(log, log_cap, "%s shader failed to compile (no driver log)",
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment");
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

nw_result nw_gfx_program_build(nw_gfx_program *out, const char *vertex_src,
                               const char *fragment_src) {
    if (!out || !vertex_src || !fragment_src) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: null program args");
    }

    memset(out, 0, sizeof(*out));

    GLuint vs = compile_stage(GL_VERTEX_SHADER, vertex_src, out->log, sizeof(out->log));
    if (vs == 0) {
        return nw_err(NW_ERR_GL, "gfx: vertex shader failed to compile");
    }

    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fragment_src, out->log, sizeof(out->log));
    if (fs == 0) {
        glDeleteShader(vs);
        return nw_err(NW_ERR_GL, "gfx: fragment shader failed to compile");
    }

    GLuint prog = glCreateProgram();
    if (prog == 0) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        snprintf(out->log, sizeof(out->log), "glCreateProgram failed");
        return nw_err(NW_ERR_GL, "gfx: glCreateProgram failed");
    }

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "position");
    glLinkProgram(prog);

    /* The stages are reference-counted by the program, so they can go now
     * whether or not the link worked. */
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            GLsizei written = 0;
            glGetProgramInfoLog(prog, (GLsizei)sizeof(out->log) - 1, &written, out->log);
            out->log[written < (GLsizei)sizeof(out->log) ? written
                                                         : (GLsizei)sizeof(out->log) - 1] = '\0';
        } else {
            snprintf(out->log, sizeof(out->log), "program failed to link (no driver log)");
        }
        glDeleteProgram(prog);
        return nw_err(NW_ERR_GL, "gfx: program failed to link");
    }

    out->id = prog;
    return nw_ok();
}

void nw_gfx_program_destroy(nw_gfx_program *prog) {
    if (!prog) {
        return;
    }
    if (prog->id) {
        glDeleteProgram(prog->id);
    }
    memset(prog, 0, sizeof(*prog));
}

GLint nw_gfx_program_uniform(const nw_gfx_program *prog, const char *name) {
    if (!prog || !prog->id || !name) {
        return -1;
    }
    return glGetUniformLocation(prog->id, name);
}

/* ==========================================================================
 * Textures
 * ========================================================================== */

static void apply_filter(nw_gfx_filter filter) {
    GLint min_filter;
    switch (filter) {
        case NW_GFX_FILTER_NEAREST:
            min_filter = GL_NEAREST;
            break;
        case NW_GFX_FILTER_MIPMAP:
            min_filter = GL_LINEAR_MIPMAP_LINEAR;
            break;
        case NW_GFX_FILTER_LINEAR:
        default:
            min_filter = GL_LINEAR;
            break;
    }
    GLint mag_filter = (filter == NW_GFX_FILTER_NEAREST) ? GL_NEAREST : GL_LINEAR;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
}

static void apply_wrap(nw_gfx_wrap wrap) {
    GLint mode = (wrap == NW_GFX_WRAP_REPEAT) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
}

nw_result nw_gfx_texture_create(nw_gfx_texture *out, int width, int height,
                                nw_gfx_format format, nw_gfx_filter filter,
                                nw_gfx_wrap wrap, const void *pixels) {
    if (!out) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: null texture");
    }
    /* A zero-sized texture is a caller bug that GL reports much later and much
     * more confusingly, so reject it here. */
    if (width <= 0 || height <= 0) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: texture needs positive dimensions");
    }

    memset(out, 0, sizeof(*out));

    GLuint id = 0;
    glGenTextures(1, &id);
    if (id == 0) {
        return nw_err(NW_ERR_GL, "gfx: glGenTextures failed");
    }

    const format_info *fi = format_lookup(format);

    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, fi->internal_format, width, height, 0, fi->format, fi->type,
                 pixels);
    apply_filter(filter);
    apply_wrap(wrap);

    if (filter == NW_GFX_FILTER_MIPMAP) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    out->id     = id;
    out->width  = width;
    out->height = height;
    out->format = format;
    out->filter = filter;
    out->wrap   = wrap;
    return nw_ok();
}

nw_result nw_gfx_texture_resize(nw_gfx_texture *tex, int width, int height) {
    if (!tex || !tex->id) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: resize of uncreated texture");
    }
    if (width <= 0 || height <= 0) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: texture needs positive dimensions");
    }
    /* Resize paths run on every output configure event; most are no-ops and
     * reallocating VRAM for them would cause visible hitching. */
    if (tex->width == width && tex->height == height) {
        return nw_ok();
    }

    const format_info *fi = format_lookup(tex->format);

    glBindTexture(GL_TEXTURE_2D, tex->id);
    glTexImage2D(GL_TEXTURE_2D, 0, fi->internal_format, width, height, 0, fi->format, fi->type,
                 NULL);
    /* Reallocation drops sampler state on some drivers, so reapply. */
    apply_filter(tex->filter);
    apply_wrap(tex->wrap);
    glBindTexture(GL_TEXTURE_2D, 0);

    tex->width  = width;
    tex->height = height;
    return nw_ok();
}

void nw_gfx_texture_set_filter(nw_gfx_texture *tex, nw_gfx_filter filter) {
    if (!tex || !tex->id || tex->filter == filter) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, tex->id);
    apply_filter(filter);
    /* Switching to mipmap sampling without a chain present samples black, so
     * build it here rather than relying on the caller to remember. */
    if (filter == NW_GFX_FILTER_MIPMAP) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    tex->filter = filter;
}

void nw_gfx_texture_gen_mipmaps(nw_gfx_texture *tex) {
    if (!tex || !tex->id || tex->filter != NW_GFX_FILTER_MIPMAP) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, tex->id);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void nw_gfx_texture_destroy(nw_gfx_texture *tex) {
    if (!tex) {
        return;
    }
    if (tex->id) {
        glDeleteTextures(1, &tex->id);
    }
    memset(tex, 0, sizeof(*tex));
}

void nw_gfx_texture_bind(const nw_gfx_texture *tex, int unit) {
    glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, tex ? tex->id : 0);
}

/* ==========================================================================
 * Render targets
 * ========================================================================== */

nw_result nw_gfx_target_create(nw_gfx_target *out, int width, int height,
                               nw_gfx_format format, nw_gfx_filter filter, bool feedback) {
    if (!out) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: null target");
    }

    memset(out, 0, sizeof(*out));

    int count = feedback ? 2 : 1;
    for (int i = 0; i < count; i++) {
        nw_result r = nw_gfx_texture_create(&out->textures[i], width, height, format, filter,
                                            NW_GFX_WRAP_CLAMP, NULL);
        if (nw_is_err(r)) {
            nw_gfx_target_destroy(out);
            return r;
        }
    }

    glGenFramebuffers(1, &out->fbo);
    if (out->fbo == 0) {
        nw_gfx_target_destroy(out);
        return nw_err(NW_ERR_GL, "gfx: glGenFramebuffers failed");
    }

    /* Validate once at creation. A target that is going to be incomplete is
     * better caught here than as silently black output every frame. */
    glBindFramebuffer(GL_FRAMEBUFFER, out->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           out->textures[0].id, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        nw_gfx_target_destroy(out);
        return nw_err(NW_ERR_GL, "gfx: framebuffer incomplete");
    }

    out->feedback    = feedback;
    out->front       = 0;
    out->needs_clear = true;
    return nw_ok();
}

nw_result nw_gfx_target_resize(nw_gfx_target *target, int width, int height) {
    if (!target || !target->fbo) {
        return nw_err(NW_ERR_INVALID_ARG, "gfx: resize of uncreated target");
    }
    if (target->textures[0].width == width && target->textures[0].height == height) {
        return nw_ok();
    }

    int count = target->feedback ? 2 : 1;
    for (int i = 0; i < count; i++) {
        nw_result r = nw_gfx_texture_resize(&target->textures[i], width, height);
        if (nw_is_err(r)) {
            return r;
        }
    }

    /* Contents are undefined after reallocation. For a feedback target that
     * means the history is garbage, which shows up as a flash of noise unless
     * it is cleared before the next read. */
    target->needs_clear = true;
    return nw_ok();
}

void nw_gfx_target_destroy(nw_gfx_target *target) {
    if (!target) {
        return;
    }
    if (target->fbo) {
        glDeleteFramebuffers(1, &target->fbo);
    }
    nw_gfx_texture_destroy(&target->textures[0]);
    nw_gfx_texture_destroy(&target->textures[1]);
    memset(target, 0, sizeof(*target));
}

void nw_gfx_target_bind(nw_gfx_target *target) {
    if (!target || !target->fbo) {
        return;
    }

    const nw_gfx_texture *write = nw_gfx_target_write(target);

    glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, write->id, 0);
    glViewport(0, 0, write->width, write->height);

    if (target->needs_clear) {
        /* Clear both halves of a feedback pair: the one not being written this
         * frame is what gets sampled next frame. */
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (target->feedback) {
            const nw_gfx_texture *other = nw_gfx_target_read(target);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   other->id, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   write->id, 0);
        }
        target->needs_clear = false;
    }
}

void nw_gfx_target_unbind(void) { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void nw_gfx_target_swap(nw_gfx_target *target) {
    if (!target || !target->feedback) {
        return;
    }
    target->front ^= 1;
}

const nw_gfx_texture *nw_gfx_target_read(const nw_gfx_target *target) {
    if (!target || !target->textures[0].id) {
        return NULL;
    }
    /* Single-buffered: the only texture. Feedback: the one NOT being written,
     * i.e. last frame's result. */
    if (!target->feedback) {
        return &target->textures[0];
    }
    return &target->textures[target->front ^ 1];
}

const nw_gfx_texture *nw_gfx_target_write(const nw_gfx_target *target) {
    if (!target || !target->textures[0].id) {
        return NULL;
    }
    if (!target->feedback) {
        return &target->textures[0];
    }
    return &target->textures[target->front];
}

void nw_gfx_target_mark_clear(nw_gfx_target *target) {
    if (target) {
        target->needs_clear = true;
    }
}

/* ==========================================================================
 * Misc
 * ========================================================================== */

bool nw_gfx_check_errors(const char *where) {
    bool found = false;
    /* Bounded: a lost context can report errors forever. */
    for (int i = 0; i < 16; i++) {
        GLenum err = glGetError();
        if (err == GL_NO_ERROR) {
            break;
        }
        found = true;
        log_error("GL error 0x%04x at %s", err, where ? where : "?");
    }
    return found;
}
