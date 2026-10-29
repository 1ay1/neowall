/* neowall/shader/shader_source.h — the shader source model. See docs/SHADERS.md.
 *
 * Two halves, so most of it is testable without a GPU:
 *
 *   nw_shader_source   GL-free. A .glsl file (plus optional .neowall
 *                      sidecar) parsed into passes, bindings, params and
 *                      target settings, with every #include expanded and a
 *                      map from generated lines back to file:line.
 *   nw_engine (engine.h) the compiled, runnable thing: GL programs and
 *                      render targets, drawing a frame into the current
 *                      framebuffer.
 *
 * Both are loop-thread objects (GL is). */
#ifndef NEOWALL_SHADER_SOURCE_H
#define NEOWALL_SHADER_SOURCE_H

#include "neowall/base.h"

/* ── what a pass reads ───────────────────────────────────────────────── */

typedef enum {
    NW_IN_NONE,     /* samples black */
    NW_IN_PASS,     /* another pass's output (this frame, or last if it's itself / later) */
    NW_IN_NOISE,    /* 256x256 RGBA noise, repeat */
    NW_IN_AUDIO,    /* 512x2: spectrum row, waveform row */
    NW_IN_FONT,     /* 1024x1024 SDF glyph atlas (Shadertoy layout) */
    NW_IN_KEYBOARD, /* 256x3 key state (Shadertoy layout) */
    NW_IN_IMAGE,    /* a texture file */
    NW_IN_BLUE_NOISE,
    NW_IN_CUBE,     /* a cube pass's output (Shadertoy "Cubemap A"): a samplerCube */
    NW_IN_SKY,      /* a built-in sky cube, for shaders that sample one of
                     * Shadertoy's cube maps (Uffizi, Forest, ...) we can't ship */
} nw_input_kind;

typedef struct {
    nw_input_kind kind;
    int pass;            /* NW_IN_PASS, NW_IN_CUBE: index into passes */
    const char *path;    /* NW_IN_IMAGE: resolved path */
    bool filter_nearest; /* sampler hints (Shadertoy "filter"/"wrap") */
    bool wrap_repeat;
} nw_input;

typedef enum { NW_FMT_RGBA8, NW_FMT_RGBA16F, NW_FMT_RGBA32F } nw_format;
typedef enum { NW_FILTER_LINEAR, NW_FILTER_NEAREST, NW_FILTER_MIPMAP } nw_filter;
typedef enum { NW_WRAP_CLAMP, NW_WRAP_REPEAT, NW_WRAP_MIRROR } nw_wrap;

#define NW_CHANNELS 4

typedef struct {
    const char *name;        /* "Image", "Buffer A", or a manifest name */
    const char *body;        /* this pass's GLSL, includes expanded */
    int first_line;          /* body line 1 = this line in `file` */
    const char *file;
    bool is_image;           /* the pass that reaches the screen */
    bool is_cube;            /* mainCubemap: renders the six faces of a cube */
    nw_input ch[NW_CHANNELS];
    nw_format format;
    double scale;            /* of the output size; ignored when fixed */
    int fixed_w, fixed_h;    /* 0 = follow the output */
    nw_filter filter;
    nw_wrap wrap;
} nw_pass_src;

/* A `// @param` tweakable. Declared by the engine; live-settable. */
typedef struct {
    const char *name;
    const char *type;        /* "float" "vec2" "vec3" "vec4" "int" "bool" */
    float def[4];
    float min, max, step;    /* 0,0,0 = unbounded */
    const char *help;
} nw_param;

/* A manifest `uniforms { uWind cpu }` binding to a live source. */
typedef struct {
    const char *name;
    const char *source;      /* source spec text, e.g. "cpu", "0.8", "exec(...)" */
} nw_uniform_bind;

/* One line of generated GLSL -> where the author wrote it. */
typedef struct {
    const char *file;
    int line;
} nw_src_loc;

typedef struct nw_shader_source {
    const char *path;            /* the .glsl */
    const char *common;          /* shared by every pass, may be NULL */
    int common_first_line;
    const char *common_file;
    NW_LIST(nw_pass_src) passes;  /* in execution order (sorted) */
    int image_pass;
    NW_LIST(nw_param) params;
    NW_LIST(nw_uniform_bind) binds;
    nw_strs includes;            /* every .glsl read; [0] = the shader */
    const char *manifest;        /* the .neowall read, or NULL */
    const char *state_pass;      /* manifest `state <pass>`, or NULL */
    bool explicit_bindings;      /* a manifest set channels; no guessing */
    bool animated;               /* uses time/mouse/audio/feedback */
    nw_arena arena_;
} nw_shader_source;

/* Load `path` (a .glsl, or a .neowall naming one) and its sidecar manifest.
 * Errors are logged as file:line and returned; *out untouched on failure. */
NW_MUST_USE nw_result nw_shader_source_load(const char *path, nw_shader_source **out);
/* Same, from text in memory (tests). `name` is used for messages and as the
 * base for relative includes (pass NULL to forbid includes). */
NW_MUST_USE nw_result nw_shader_source_parse(const char *text, const char *name,
                                             const char *manifest_text, nw_shader_source **out);
void nw_shader_source_free(nw_shader_source *src);

int nw_shader_source_find_pass(const nw_shader_source *src, const char *name);
/* false if `text` has `#pragma neowall requires X.Y` newer than this build
 * (logged). Its uniforms would read 0, so it's refused, not drawn wrong. */
bool nw_shader_requires_ok(const char *text, const char *path);

/* ── the generated program text ──────────────────────────────────────── */

/* Assemble a pass's full fragment source: version, uniforms, stdlib, params,
 * common, body, main. *map gets one entry per generated line (NULL entry
 * file = injected code). Caller frees text and map. */
NW_MUST_USE nw_result nw_shader_assemble(const nw_shader_source *src, int pass, char **text,
                                         nw_src_loc **map, size_t *map_len);

/* Every chunk of GLSL put in front of a shader (uniforms, stdlib), for
 * tools that need the names that exist: `neowall docs`, "did you mean". */
size_t nw_shader_preamble_chunks(const char **out, size_t max);

#endif /* NEOWALL_SHADER_SOURCE_H */
