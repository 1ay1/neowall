/* neowall/shader/engine.h — runs an nw_shader_source on the GPU.
 *
 *   nw_engine_new(src)       compile every pass. On error nothing is
 *                            returned and the caller keeps its old engine
 *                            (a broken save never replaces what's shown).
 *   nw_engine_frame(e, in)   draw one frame into the currently bound
 *                            framebuffer at in->width x in->height.
 *
 * `// @param` uniforms get their declared defaults.
 * Needs a current GL 3.3 core context. */
#ifndef NEOWALL_SHADER_ENGINE_H
#define NEOWALL_SHADER_ENGINE_H

#include "neowall/base.h"
#include "neowall/shader/shader_source.h"

typedef struct nw_engine nw_engine;

/* Everything a frame depends on, filled in by the app. Plain data. */
typedef struct {
    int width, height;          /* the output, pixels */
    double time;                /* iTime, seconds (already frame-stamped) */
    double dt;                  /* iTimeDelta */
    int frame;                  /* iFrame */
    float mouse[4];             /* iMouse: x, y, click x, click y (Shadertoy sign rules) */
    float date[4];              /* iDate: year, month, day, seconds of day */
    float span_offset[2];       /* iSpanOffset */
    /* the whole span this output is a slice of, or 0x0 when it isn't spanned:
     * buffer passes are sized to it and the image pass reports it as
     * iResolution, so one scene runs across every monitor */
    int span_w, span_h;
    float fps;                  /* iFrameRate; 0 = derive from dt */
    /* name -> value for every other float uniform (reactive, manifest
     * bindings). Looked up by name once per program, cached. */
    const struct nw_uniform_value *uniforms;
    size_t uniform_count;
    /* a second list, for values that belong to the output (iWindows) */
    const struct nw_uniform_value *uniforms2;
    size_t uniform2_count;
    /* audio texture rows (spectrum, waveform), 512 each, or NULL */
    const float *audio_spectrum, *audio_waveform;
    const uint8_t *keyboard;    /* 256x3 bytes (down, pressed, toggled), or NULL */
    /* extra samplers by name (the terminal's iTermCells, ...), bound after
     * the four channels */
    const struct nw_texture_value *textures;
    size_t texture_count;
    /* the config's `channels`: a nonzero texture here replaces what channel
     * N of every pass would read, unless the manifest bound it */
    unsigned channel_override[4];
    float channel_override_size[4][2];
} nw_frame_in;

typedef struct nw_texture_value {
    const char *name;
    unsigned tex;               /* GL texture name; 0 binds nothing */
} nw_texture_value;

typedef struct nw_uniform_value {
    const char *name;
    int count;                  /* 1..4 floats, or N vec4 for arrays */
    const float *v;
    bool is_int;
    bool float_array;           /* count separate floats (float[N]) */
} nw_uniform_value;

/* Compile every pass (in parallel where the driver can). On failure nothing
 * is returned and nw_engine_last_errors() says why. `src` must outlive the
 * engine. */
NW_MUST_USE nw_result nw_engine_new(const nw_shader_source *src, nw_engine **out);
void nw_engine_free(nw_engine *e);

/* Draw. The image pass goes to the framebuffer bound when this is called. */
void nw_engine_frame(nw_engine *e, const nw_frame_in *in);

/* Compile errors from the last failed nw_engine_new, formatted
 * "file:line: message" with the source line and a caret. Owned by the
 * engine module; valid until the next nw_engine_new. */
const char *nw_engine_last_errors(void);

/* The first 4 texels the manifest's `state` pass wrote last frame, as 16
 * floats. false if there's no state pass or it's the image pass. Stalls the
 * GPU; call it rarely. */
bool nw_engine_read_state(nw_engine *e, float out[16]);

/* Whether the shader changes over time (else it can be drawn once). */
bool nw_engine_animated(const nw_engine *e);

#endif /* NEOWALL_SHADER_ENGINE_H */
