/* Render Optimizer - High-Performance Multipass Rendering Optimizations
 * 
 * Implementation of GPU state caching, uniform deduplication, buffer analysis,
 * pass culling, and temporal optimization techniques.
 */

#include "render_optimizer.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* Forward declarations for logging (from neowall's log system) */
extern void log_info(const char *fmt, ...);
extern void log_debug(const char *fmt, ...);
extern void log_error(const char *fmt, ...);

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static double get_wall_time(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
#endif
}

static program_uniform_cache_t *find_or_create_program_cache(render_optimizer_t *opt, GLuint program) {
    if (!opt || program == 0) return NULL;
    
    /* Look for existing cache */
    for (int i = 0; i < opt->uniform_cache_count; i++) {
        if (opt->uniform_caches[i].program == program && opt->uniform_caches[i].valid) {
            return &opt->uniform_caches[i];
        }
    }
    
    /* Create new cache if space available */
    if (opt->uniform_cache_count < OPT_MAX_PROGRAMS) {
        program_uniform_cache_t *cache = &opt->uniform_caches[opt->uniform_cache_count++];
        memset(cache, 0, sizeof(*cache));
        cache->program = program;
        cache->valid = true;
        return cache;
    }
    
    /* No space - return NULL (will fall through to non-cached path) */
    return NULL;
}

static cached_uniform_t *find_uniform_in_cache(program_uniform_cache_t *cache, GLint location) {
    if (!cache) return NULL;
    
    for (int i = 0; i < cache->uniform_count; i++) {
        if (cache->uniforms[i].location == location && cache->uniforms[i].valid) {
            return &cache->uniforms[i];
        }
    }
    return NULL;
}

static cached_uniform_t *get_or_create_uniform(program_uniform_cache_t *cache, GLint location, uniform_type_t type) {
    if (!cache) return NULL;
    
    /* Find existing */
    cached_uniform_t *uniform = find_uniform_in_cache(cache, location);
    if (uniform) return uniform;
    
    /* Create new if space available */
    if (cache->uniform_count < OPT_MAX_CACHED_UNIFORMS) {
        uniform = &cache->uniforms[cache->uniform_count++];
        memset(uniform, 0, sizeof(*uniform));
        uniform->location = location;
        uniform->type = type;
        uniform->valid = true;
        return uniform;
    }
    
    return NULL;
}

/* Count occurrences of a substring in source */
static int count_pattern(const char *source, const char *pattern) {
    if (!source || !pattern) return 0;
    
    int count = 0;
    const char *p = source;
    size_t len = strlen(pattern);
    
    while ((p = strstr(p, pattern)) != NULL) {
        count++;
        p += len;
    }
    return count;
}

/* Check if source contains pattern */
static bool contains_pattern(const char *source, const char *pattern) {
    return source && pattern && strstr(source, pattern) != NULL;
}

/* ============================================================================
 * Initialization and Lifecycle
 * ============================================================================ */

void render_optimizer_init(render_optimizer_t *opt) {
    if (!opt) return;
    
    memset(opt, 0, sizeof(*opt));
    
    /* Initialize GPU state cache with invalid values to force first-time set */
    opt->state.current_program = 0;
    opt->state.current_vao = 0;
    opt->state.current_vbo = 0;
    opt->state.current_fbo = 0xFFFFFFFF;  /* Invalid to force first bind */
    opt->state.active_texture_unit = 0xFFFFFFFF;
    
    for (int i = 0; i < OPT_MAX_TEXTURE_UNITS; i++) {
        opt->state.bound_textures[i] = 0xFFFFFFFF;
        opt->state.texture_targets[i] = 0;
    }
    
    opt->state.depth_test_enabled = false;
    opt->state.blend_enabled = false;
    opt->state.cull_face_enabled = false;
    opt->state.scissor_test_enabled = false;
    opt->state.depth_mask = true;
    
    /* Invalid viewport to force first set */
    opt->state.viewport[0] = -1;
    opt->state.viewport[1] = -1;
    opt->state.viewport[2] = -1;
    opt->state.viewport[3] = -1;
    
    /* Initialize temporal states */
    for (int i = 0; i < 5; i++) {
        temporal_init(&opt->temporal[i], TEMPORAL_MODE_AUTO);
    }
    
    /* Initialize cull states */
    for (int i = 0; i < 5; i++) {
        pass_cull_init(&opt->cull_state[i], PASS_CULL_AUTO);
    }
    
    /* Default settings */
    opt->enabled = true;
    opt->aggressive_mode = false;
    opt->quality_bias = 0.7f;  /* Slight bias towards quality */
    opt->global_temporal_mode = TEMPORAL_MODE_AUTO;
    
    opt->state.initialized = true;
    opt->initialized = true;
    
    log_info("Render optimizer initialized");
}

void render_optimizer_destroy(render_optimizer_t *opt) {
    if (!opt) return;
    
    /* Destroy temporal resources */
    for (int i = 0; i < 5; i++) {
        temporal_destroy(&opt->temporal[i]);
    }
    
    memset(opt, 0, sizeof(*opt));
    log_info("Render optimizer destroyed");
}

void render_optimizer_reset(render_optimizer_t *opt) {
    if (!opt) return;
    
    /* Reset uniform caches */
    for (int i = 0; i < opt->uniform_cache_count; i++) {
        opt->uniform_caches[i].uniform_count = 0;
    }
    
    /* Reset statistics */
    memset(&opt->stats, 0, sizeof(opt->stats));
    
    /* Reset temporal states */
    for (int i = 0; i < 5; i++) {
        temporal_mode_t mode = opt->temporal[i].mode;
        temporal_destroy(&opt->temporal[i]);
        temporal_init(&opt->temporal[i], mode);
    }
    
    /* Invalidate state cache to force re-sync */
    opt->state.current_fbo = 0xFFFFFFFF;
    opt->state.active_texture_unit = 0xFFFFFFFF;
    for (int i = 0; i < OPT_MAX_TEXTURE_UNITS; i++) {
        opt->state.bound_textures[i] = 0xFFFFFFFF;
    }
    
    log_info("Render optimizer reset");
}

void render_optimizer_set_enabled(render_optimizer_t *opt, bool enabled) {
    if (opt) opt->enabled = enabled;
}

void render_optimizer_set_quality_bias(render_optimizer_t *opt, float bias) {
    if (opt) {
        opt->quality_bias = bias < 0.0f ? 0.0f : (bias > 1.0f ? 1.0f : bias);
    }
}

/* ============================================================================
 * Frame Lifecycle
 * ============================================================================ */

void render_optimizer_begin_frame(render_optimizer_t *opt, 
                                   float time, 
                                   float mouse_x, float mouse_y, 
                                   bool mouse_click) {
    if (!opt || !opt->initialized) return;
    
    double wall_time = get_wall_time();
    
    /* Track frame timing */
    if (opt->last_frame_time > 0.0) {
        opt->frame_time_ms = (float)(wall_time - opt->last_frame_time) * 1000.0f;
    }
    opt->frame_start_time = wall_time;
    opt->frame_number++;
    
    /* Track mouse movement */
    float mouse_delta = fabsf(mouse_x - opt->mouse_x) + fabsf(mouse_y - opt->mouse_y);
    if (mouse_delta > 0.5f) {
        opt->mouse_last_move_time = wall_time;
    }
    opt->mouse_idle_seconds = (float)(wall_time - opt->mouse_last_move_time);
    
    opt->mouse_x = mouse_x;
    opt->mouse_y = mouse_y;
    opt->mouse_click = mouse_click;
    
    /* Update temporal states */
    for (int i = 0; i < 5; i++) {
        temporal_update(&opt->temporal[i], time, mouse_x, mouse_y, mouse_click, wall_time);
    }
}

void render_optimizer_end_frame(render_optimizer_t *opt) {
    if (!opt || !opt->initialized) return;
    
    double wall_time = get_wall_time();
    opt->last_frame_time = wall_time;
    
    /* Update temporal frame tracking */
    for (int i = 0; i < 5; i++) {
        if (!opt->temporal[i].skip_this_frame) {
            temporal_frame_rendered(&opt->temporal[i], wall_time);
        }
    }
    
    /* Log stats periodically (every 300 frames) */
    if (opt->frame_number % 300 == 0 && opt->frame_number > 0) {
        render_optimizer_log_stats(opt);
    }
}

/* ============================================================================
 * Optimized GL State Functions
 * ============================================================================ */

void opt_use_program(render_optimizer_t *opt, GLuint program) {
    if (!opt || !opt->enabled) {
        glUseProgram(program);
        return;
    }
    
    opt->stats.program_switches_total++;
    
    if (opt->state.current_program == program) {
        opt->stats.program_switches_avoided++;
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glUseProgram(program);
    opt->state.current_program = program;
    opt->stats.gl_calls_total++;
}

void opt_bind_vao(render_optimizer_t *opt, GLuint vao) {
    if (!opt || !opt->enabled) {
        glBindVertexArray(vao);
        return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (opt->state.current_vao == vao) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glBindVertexArray(vao);
    opt->state.current_vao = vao;
}

void opt_bind_buffer(render_optimizer_t *opt, GLenum target, GLuint buffer) {
    if (!opt || !opt->enabled) {
        glBindBuffer(target, buffer);
        return;
    }
    
    /* For now, only cache GL_ARRAY_BUFFER */
    if (target == GL_ARRAY_BUFFER) {
        opt->stats.gl_calls_total++;
        
        if (opt->state.current_vbo == buffer) {
            opt->stats.gl_calls_avoided++;
            return;
        }
        
        glBindBuffer(target, buffer);
        opt->state.current_vbo = buffer;
    } else {
        glBindBuffer(target, buffer);
    }
}

void opt_bind_framebuffer(render_optimizer_t *opt, GLenum target, GLuint fbo) {
    if (!opt || !opt->enabled) {
        glBindFramebuffer(target, fbo);
        return;
    }
    
    opt->stats.fbo_binds_total++;
    
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        if (opt->state.current_fbo == fbo) {
            opt->stats.fbo_binds_avoided++;
            opt->stats.gl_calls_avoided++;
            return;
        }
        opt->state.current_fbo = fbo;
    }
    
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) {
        if (target == GL_READ_FRAMEBUFFER && opt->state.current_read_fbo == fbo) {
            opt->stats.fbo_binds_avoided++;
            opt->stats.gl_calls_avoided++;
            return;
        }
        opt->state.current_read_fbo = fbo;
    }
    
    glBindFramebuffer(target, fbo);
    opt->stats.gl_calls_total++;
}

void opt_bind_texture(render_optimizer_t *opt, int unit, GLenum target, GLuint texture) {
    if (!opt || !opt->enabled || unit < 0 || unit >= OPT_MAX_TEXTURE_UNITS) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(target, texture);
        return;
    }
    
    opt->stats.texture_binds_total++;
    
    /* Check if already bound */
    if (opt->state.bound_textures[unit] == texture && 
        opt->state.texture_targets[unit] == target) {
        opt->stats.texture_binds_avoided++;
        opt->stats.gl_calls_avoided++;
        /* Still need to ensure correct unit is active for uniform setup */
        opt_active_texture(opt, unit);
        return;
    }
    
    opt_active_texture(opt, unit);
    glBindTexture(target, texture);
    
    opt->state.bound_textures[unit] = texture;
    opt->state.texture_targets[unit] = target;
    opt->stats.gl_calls_total++;
}

void opt_active_texture(render_optimizer_t *opt, int unit) {
    if (!opt || !opt->enabled) {
        glActiveTexture(GL_TEXTURE0 + unit);
        return;
    }
    
    GLenum gl_unit = GL_TEXTURE0 + unit;
    
    if (opt->state.active_texture_unit == gl_unit) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glActiveTexture(gl_unit);
    opt->state.active_texture_unit = gl_unit;
    opt->stats.gl_calls_total++;
}

/* ============================================================================
 * Optimized Render State Functions
 * ============================================================================ */

void opt_enable(render_optimizer_t *opt, GLenum cap) {
    if (!opt || !opt->enabled) {
        glEnable(cap);
        return;
    }
    
    bool *cached = NULL;
    
    switch (cap) {
        case GL_DEPTH_TEST:
            cached = &opt->state.depth_test_enabled;
            break;
        case GL_BLEND:
            cached = &opt->state.blend_enabled;
            break;
        case GL_CULL_FACE:
            cached = &opt->state.cull_face_enabled;
            break;
        case GL_SCISSOR_TEST:
            cached = &opt->state.scissor_test_enabled;
            break;
        default:
            glEnable(cap);
            return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (cached && *cached) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glEnable(cap);
    if (cached) *cached = true;
}

void opt_disable(render_optimizer_t *opt, GLenum cap) {
    if (!opt || !opt->enabled) {
        glDisable(cap);
        return;
    }
    
    bool *cached = NULL;
    
    switch (cap) {
        case GL_DEPTH_TEST:
            cached = &opt->state.depth_test_enabled;
            break;
        case GL_BLEND:
            cached = &opt->state.blend_enabled;
            break;
        case GL_CULL_FACE:
            cached = &opt->state.cull_face_enabled;
            break;
        case GL_SCISSOR_TEST:
            cached = &opt->state.scissor_test_enabled;
            break;
        default:
            glDisable(cap);
            return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (cached && !*cached) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glDisable(cap);
    if (cached) *cached = false;
}

void opt_depth_mask(render_optimizer_t *opt, GLboolean flag) {
    if (!opt || !opt->enabled) {
        glDepthMask(flag);
        return;
    }
    
    opt->stats.gl_calls_total++;
    
    bool new_val = (flag == GL_TRUE);
    if (opt->state.depth_mask == new_val) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glDepthMask(flag);
    opt->state.depth_mask = new_val;
}

void opt_color_mask(render_optimizer_t *opt, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (!opt || !opt->enabled) {
        glColorMask(r, g, b, a);
        return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (opt->state.color_mask[0] == r && 
        opt->state.color_mask[1] == g &&
        opt->state.color_mask[2] == b && 
        opt->state.color_mask[3] == a) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glColorMask(r, g, b, a);
    opt->state.color_mask[0] = r;
    opt->state.color_mask[1] = g;
    opt->state.color_mask[2] = b;
    opt->state.color_mask[3] = a;
}

void opt_blend_func(render_optimizer_t *opt, GLenum sfactor, GLenum dfactor) {
    opt_blend_func_separate(opt, sfactor, dfactor, sfactor, dfactor);
}

void opt_blend_func_separate(render_optimizer_t *opt, 
                              GLenum srcRGB, GLenum dstRGB, 
                              GLenum srcAlpha, GLenum dstAlpha) {
    if (!opt || !opt->enabled) {
        glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
        return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (opt->state.blend_src_rgb == srcRGB &&
        opt->state.blend_dst_rgb == dstRGB &&
        opt->state.blend_src_alpha == srcAlpha &&
        opt->state.blend_dst_alpha == dstAlpha) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
    opt->state.blend_src_rgb = srcRGB;
    opt->state.blend_dst_rgb = dstRGB;
    opt->state.blend_src_alpha = srcAlpha;
    opt->state.blend_dst_alpha = dstAlpha;
}

void opt_viewport(render_optimizer_t *opt, GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!opt || !opt->enabled) {
        glViewport(x, y, width, height);
        return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (opt->state.viewport[0] == x &&
        opt->state.viewport[1] == y &&
        opt->state.viewport[2] == width &&
        opt->state.viewport[3] == height) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glViewport(x, y, width, height);
    opt->state.viewport[0] = x;
    opt->state.viewport[1] = y;
    opt->state.viewport[2] = width;
    opt->state.viewport[3] = height;
}

void opt_clear_color(render_optimizer_t *opt, GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (!opt || !opt->enabled) {
        glClearColor(r, g, b, a);
        return;
    }
    
    opt->stats.gl_calls_total++;
    
    if (opt_float_eq(opt->state.clear_color[0], r, 0.0001f) &&
        opt_float_eq(opt->state.clear_color[1], g, 0.0001f) &&
        opt_float_eq(opt->state.clear_color[2], b, 0.0001f) &&
        opt_float_eq(opt->state.clear_color[3], a, 0.0001f)) {
        opt->stats.gl_calls_avoided++;
        return;
    }
    
    glClearColor(r, g, b, a);
    opt->state.clear_color[0] = r;
    opt->state.clear_color[1] = g;
    opt->state.clear_color[2] = b;
    opt->state.clear_color[3] = a;
}

/* ============================================================================
 * Optimized Uniform Functions
 * ============================================================================ */

bool opt_uniform_1f(render_optimizer_t *opt, GLuint program, GLint location, float v) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform1f(location, v);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_FLOAT);
    
    if (uniform && uniform->valid) {
        float *cached_val = (float *)uniform->value;
        if (opt_float_eq(*cached_val, v, 0.0001f)) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        *cached_val = v;
    }
    
    glUniform1f(location, v);
    return true;
}

bool opt_uniform_2f(render_optimizer_t *opt, GLuint program, GLint location, float v0, float v1) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform2f(location, v0, v1);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_VEC2);
    
    if (uniform && uniform->valid) {
        float *cached_val = (float *)uniform->value;
        if (opt_float_eq(cached_val[0], v0, 0.0001f) &&
            opt_float_eq(cached_val[1], v1, 0.0001f)) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        cached_val[0] = v0;
        cached_val[1] = v1;
    }
    
    glUniform2f(location, v0, v1);
    return true;
}

bool opt_uniform_3f(render_optimizer_t *opt, GLuint program, GLint location, 
                    float v0, float v1, float v2) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform3f(location, v0, v1, v2);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_VEC3);
    
    if (uniform && uniform->valid) {
        float *cached_val = (float *)uniform->value;
        if (opt_float_eq(cached_val[0], v0, 0.0001f) &&
            opt_float_eq(cached_val[1], v1, 0.0001f) &&
            opt_float_eq(cached_val[2], v2, 0.0001f)) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        cached_val[0] = v0;
        cached_val[1] = v1;
        cached_val[2] = v2;
    }
    
    glUniform3f(location, v0, v1, v2);
    return true;
}

bool opt_uniform_4f(render_optimizer_t *opt, GLuint program, GLint location, 
                    float v0, float v1, float v2, float v3) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform4f(location, v0, v1, v2, v3);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_VEC4);
    
    if (uniform && uniform->valid) {
        float *cached_val = (float *)uniform->value;
        if (opt_float_eq(cached_val[0], v0, 0.0001f) &&
            opt_float_eq(cached_val[1], v1, 0.0001f) &&
            opt_float_eq(cached_val[2], v2, 0.0001f) &&
            opt_float_eq(cached_val[3], v3, 0.0001f)) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        cached_val[0] = v0;
        cached_val[1] = v1;
        cached_val[2] = v2;
        cached_val[3] = v3;
    }
    
    glUniform4f(location, v0, v1, v2, v3);
    return true;
}

bool opt_uniform_1i(render_optimizer_t *opt, GLuint program, GLint location, int v) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform1i(location, v);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_INT);
    
    if (uniform && uniform->valid) {
        int *cached_val = (int *)uniform->value;
        if (*cached_val == v) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        *cached_val = v;
    }
    
    glUniform1i(location, v);
    return true;
}

bool opt_uniform_2i(render_optimizer_t *opt, GLuint program, GLint location, int v0, int v1) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform2i(location, v0, v1);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_IVEC2);
    
    if (uniform && uniform->valid) {
        int *cached_val = (int *)uniform->value;
        if (cached_val[0] == v0 && cached_val[1] == v1) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        cached_val[0] = v0;
        cached_val[1] = v1;
    }
    
    glUniform2i(location, v0, v1);
    return true;
}

bool opt_uniform_3i(render_optimizer_t *opt, GLuint program, GLint location, 
                    int v0, int v1, int v2) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform3i(location, v0, v1, v2);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_IVEC3);
    
    if (uniform && uniform->valid) {
        int *cached_val = (int *)uniform->value;
        if (cached_val[0] == v0 && cached_val[1] == v1 && cached_val[2] == v2) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        cached_val[0] = v0;
        cached_val[1] = v1;
        cached_val[2] = v2;
    }
    
    glUniform3i(location, v0, v1, v2);
    return true;
}

bool opt_uniform_4i(render_optimizer_t *opt, GLuint program, GLint location, 
                    int v0, int v1, int v2, int v3) {
    if (location < 0) return false;
    
    if (!opt || !opt->enabled) {
        glUniform4i(location, v0, v1, v2, v3);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
    cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_IVEC4);
    
    if (uniform && uniform->valid) {
        int *cached_val = (int *)uniform->value;
        if (cached_val[0] == v0 && cached_val[1] == v1 && 
            cached_val[2] == v2 && cached_val[3] == v3) {
            opt->stats.uniform_updates_avoided++;
            return false;
        }
        cached_val[0] = v0;
        cached_val[1] = v1;
        cached_val[2] = v2;
        cached_val[3] = v3;
    }
    
    glUniform4i(location, v0, v1, v2, v3);
    return true;
}

bool opt_uniform_3fv(render_optimizer_t *opt, GLuint program, GLint location, 
                     int count, const float *value) {
    if (location < 0 || !value) return false;
    
    if (!opt || !opt->enabled) {
        glUniform3fv(location, count, value);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    /* Only cache single-count uniforms */
    if (count == 1) {
        program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
        cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_VEC3);
        
        if (uniform && uniform->valid) {
            float *cached_val = (float *)uniform->value;
            if (opt_float_eq(cached_val[0], value[0], 0.0001f) &&
                opt_float_eq(cached_val[1], value[1], 0.0001f) &&
                opt_float_eq(cached_val[2], value[2], 0.0001f)) {
                opt->stats.uniform_updates_avoided++;
                return false;
            }
            memcpy(cached_val, value, sizeof(float) * 3);
        }
    }
    
    glUniform3fv(location, count, value);
    return true;
}

bool opt_uniform_4fv(render_optimizer_t *opt, GLuint program, GLint location, 
                     int count, const float *value) {
    if (location < 0 || !value) return false;
    
    if (!opt || !opt->enabled) {
        glUniform4fv(location, count, value);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    /* Only cache single-count uniforms */
    if (count == 1) {
        program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
        cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_VEC4);
        
        if (uniform && uniform->valid) {
            float *cached_val = (float *)uniform->value;
            if (opt_float_eq(cached_val[0], value[0], 0.0001f) &&
                opt_float_eq(cached_val[1], value[1], 0.0001f) &&
                opt_float_eq(cached_val[2], value[2], 0.0001f) &&
                opt_float_eq(cached_val[3], value[3], 0.0001f)) {
                opt->stats.uniform_updates_avoided++;
                return false;
            }
            memcpy(cached_val, value, sizeof(float) * 4);
        }
    }
    
    glUniform4fv(location, count, value);
    return true;
}

bool opt_uniform_matrix3fv(render_optimizer_t *opt, GLuint program, GLint location, 
                            int count, bool transpose, const float *value) {
    if (location < 0 || !value) return false;
    
    if (!opt || !opt->enabled) {
        glUniformMatrix3fv(location, count, transpose ? GL_TRUE : GL_FALSE, value);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    if (count == 1) {
        program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
        cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_MAT3);
        
        if (uniform && uniform->valid) {
            float *cached_val = (float *)uniform->value;
            bool same = true;
            for (int i = 0; i < 9 && same; i++) {
                if (!opt_float_eq(cached_val[i], value[i], 0.0001f)) same = false;
            }
            if (same) {
                opt->stats.uniform_updates_avoided++;
                return false;
            }
            memcpy(cached_val, value, sizeof(float) * 9);
        }
    }
    
    glUniformMatrix3fv(location, count, transpose ? GL_TRUE : GL_FALSE, value);
    return true;
}

bool opt_uniform_matrix4fv(render_optimizer_t *opt, GLuint program, GLint location, 
                            int count, bool transpose, const float *value) {
    if (location < 0 || !value) return false;
    
    if (!opt || !opt->enabled) {
        glUniformMatrix4fv(location, count, transpose ? GL_TRUE : GL_FALSE, value);
        return true;
    }
    
    opt->stats.uniform_updates_total++;
    
    if (count == 1) {
        program_uniform_cache_t *cache = find_or_create_program_cache(opt, program);
        cached_uniform_t *uniform = get_or_create_uniform(cache, location, UNIFORM_TYPE_MAT4);
        
        if (uniform && uniform->valid) {
            float *cached_val = (float *)uniform->value;
            bool same = true;
            for (int i = 0; i < 16 && same; i++) {
                if (!opt_float_eq(cached_val[i], value[i], 0.0001f)) same = false;
            }
            if (same) {
                opt->stats.uniform_updates_avoided++;
                return false;
            }
            memcpy(cached_val, value, sizeof(float) * 16);
        }
    }
    
    glUniformMatrix4fv(location, count, transpose ? GL_TRUE : GL_FALSE, value);
    return true;
}

/* ============================================================================
 * Buffer Analysis Functions
 * ============================================================================ */

buffer_analysis_t analyze_buffer_requirements(const char *shader_source, int pass_type) {
    (void)pass_type;  /* Reserved for future use */
    
    buffer_analysis_t result;
    memset(&result, 0, sizeof(result));
    
    if (!shader_source) {
        result.hint = BUFFER_HINT_FULL;
        result.recommended_scale = 1.0f;
        return result;
    }
    
    /* Score various patterns */
    
    /* Blur indicators */
    result.blur_score += count_pattern(shader_source, "blur") * 20;
    result.blur_score += count_pattern(shader_source, "Blur") * 20;
    result.blur_score += count_pattern(shader_source, "smooth") * 10;
    result.blur_score += count_pattern(shader_source, "gaussian") * 30;
    result.blur_score += count_pattern(shader_source, "Gaussian") * 30;
    result.blur_score += count_pattern(shader_source, "box") * 5;
    result.blur_score += count_pattern(shader_source, "glow") * 15;
    result.blur_score += count_pattern(shader_source, "bloom") * 15;
    
    /* Noise indicators */
    result.noise_score += count_pattern(shader_source, "noise") * 15;
    result.noise_score += count_pattern(shader_source, "Noise") * 15;
    result.noise_score += count_pattern(shader_source, "hash") * 10;
    result.noise_score += count_pattern(shader_source, "rand") * 10;
    result.noise_score += count_pattern(shader_source, "fract(sin") * 25;
    result.noise_score += count_pattern(shader_source, "fbm") * 20;
    result.noise_score += count_pattern(shader_source, "FBM") * 20;
    
    /* Self-feedback indicators */
    result.feedback_score += count_pattern(shader_source, "iChannel0") * 5;
    result.feedback_score += count_pattern(shader_source, "texelFetch") * 3;
    result.feedback_score += count_pattern(shader_source, "textureLod") * 3;
    result.feedback_score += count_pattern(shader_source, "mix(") * 2;
    result.feedback_score += count_pattern(shader_source, "+=") * 1;
    
    /* Precision indicators (need full resolution) */
    result.precision_score += count_pattern(shader_source, "edge") * 15;
    result.precision_score += count_pattern(shader_source, "Edge") * 15;
    result.precision_score += count_pattern(shader_source, "sobel") * 20;
    result.precision_score += count_pattern(shader_source, "Sobel") * 20;
    result.precision_score += count_pattern(shader_source, "sharp") * 15;
    result.precision_score += count_pattern(shader_source, "detail") * 10;
    result.precision_score += count_pattern(shader_source, "sdf") * 15;
    result.precision_score += count_pattern(shader_source, "SDF") * 15;
    
    /* Animation indicators */
    result.animation_score += count_pattern(shader_source, "iTime") * 10;
    result.animation_score += count_pattern(shader_source, "time") * 5;
    result.animation_score += count_pattern(shader_source, "sin(") * 2;
    result.animation_score += count_pattern(shader_source, "cos(") * 2;
    
    /* Set detection flags */
    result.uses_blur = (result.blur_score >= 30);
    result.uses_noise_only = (result.noise_score >= 40 && result.precision_score < 20);
    result.uses_self_feedback = (result.feedback_score >= 10);
    result.uses_high_frequency_detail = (result.precision_score >= 30);
    result.is_time_varying = (result.animation_score >= 15);
    result.is_mouse_dependent = contains_pattern(shader_source, "iMouse");
    
    /* Determine recommendation */
    if (result.uses_noise_only && !result.uses_high_frequency_detail) {
        result.hint = BUFFER_HINT_TINY;
        result.recommended_scale = 0.125f;  /* 256px at 2K */
        result.min_resolution = 64;
    } else if (result.uses_blur && !result.uses_high_frequency_detail) {
        result.hint = BUFFER_HINT_LOW;
        result.recommended_scale = 0.25f;
        result.min_resolution = 128;
    } else if (result.uses_self_feedback && !result.uses_high_frequency_detail) {
        result.hint = BUFFER_HINT_MEDIUM;
        result.recommended_scale = 0.5f;
        result.min_resolution = 256;
    } else if (result.uses_high_frequency_detail) {
        result.hint = BUFFER_HINT_FULL;
        result.recommended_scale = 1.0f;
        result.min_resolution = 512;
    } else {
        /* Default: medium-high for unknown content */
        result.hint = BUFFER_HINT_HIGH;
        result.recommended_scale = 0.75f;
        result.min_resolution = 256;
    }
    
    return result;
}

float get_recommended_buffer_scale(const buffer_analysis_t *analysis, float base_scale) {
    if (!analysis) return base_scale;
    
    /* Combine analysis recommendation with base scale */
    float scale = analysis->recommended_scale;
    
    /* Don't go lower than base scale would suggest */
    if (scale > base_scale) {
        scale = base_scale;
    }
    
    /* Clamp to reasonable range */
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 1.0f) scale = 1.0f;
    
    return scale;
}

void update_buffer_analysis(buffer_analysis_t *analysis, 
                            int actual_width, int actual_height,
                            float render_time_ms) {
    if (!analysis) return;
    
    /* Update content hash for change detection */
    analysis->prev_content_hash = analysis->content_hash;
    analysis->content_hash = opt_hash_combine(
        opt_hash_int(actual_width),
        opt_hash_combine(opt_hash_int(actual_height), opt_hash_float(render_time_ms))
    );
    analysis->content_changed = (analysis->content_hash != analysis->prev_content_hash);
}

/* ============================================================================
 * Pass Culling Functions
 * ============================================================================ */

void pass_cull_init(pass_cull_state_t *state, pass_cull_strategy_t strategy) {
    if (!state) return;
    
    memset(state, 0, sizeof(*state));
    state->strategy = strategy;
    state->min_render_interval = 0.0;
    state->mouse_idle_threshold = 5.0f;
    state->time_delta_threshold = 0.001f;
    state->should_render = true;
}

bool pass_should_render(pass_cull_state_t *state,
                        float time, float prev_time,
                        float mouse_x, float mouse_y,
                        float prev_mouse_x, float prev_mouse_y,
                        double current_wall_time) {
    if (!state) return true;
    
    (void)prev_time;       /* Used in PASS_CULL_TIME_STATIC via state->prev_time */
    (void)prev_mouse_x;    /* Reserved for future motion tracking */
    (void)prev_mouse_y;    /* Reserved for future motion tracking */
    
    state->should_render = true;
    state->was_culled = false;
    state->cull_reason = 0;
    
    /* Update mouse tracking */
    float mouse_delta = fabsf(mouse_x - state->last_mouse_x) + fabsf(mouse_y - state->last_mouse_y);
    if (mouse_delta > 0.5f) {
        state->mouse_idle_time = 0.0;
    } else {
        state->mouse_idle_time = current_wall_time - state->last_render_time;
    }
    state->last_mouse_x = mouse_x;
    state->last_mouse_y = mouse_y;
    
    /* Check minimum interval */
    if (state->min_render_interval > 0.0 &&
        current_wall_time - state->last_render_time < state->min_render_interval) {
        state->should_render = false;
        state->was_culled = true;
        state->cull_reason = 1;  /* Too soon */
        return false;
    }
    
    /* Strategy-specific checks */
    switch (state->strategy) {
        case PASS_CULL_NONE:
            /* Never cull */
            break;
            
        case PASS_CULL_MOUSE_IDLE:
            if (state->mouse_idle_time > state->mouse_idle_threshold) {
                state->should_render = false;
                state->cull_reason = 2;  /* Mouse idle */
            }
            break;
            
        case PASS_CULL_TIME_STATIC:
            if (fabsf(time - state->prev_time) < state->time_delta_threshold) {
                state->should_render = false;
                state->cull_reason = 3;  /* Time static */
            }
            break;
            
        case PASS_CULL_CONTENT_SAME:
            if (state->curr_input_hash == state->prev_input_hash) {
                state->should_render = false;
                state->cull_reason = 4;  /* Content same */
            }
            break;
            
        case PASS_CULL_AUTO:
            /* Use heuristics: if time barely changed AND mouse idle, cull */
            if (fabsf(time - state->prev_time) < state->time_delta_threshold &&
                state->mouse_idle_time > state->mouse_idle_threshold) {
                state->should_render = false;
                state->cull_reason = 5;  /* Auto: static scene */
            }
            break;
    }
    
    state->prev_time = time;
    state->prev_input_hash = state->curr_input_hash;
    
    if (!state->should_render) {
        state->was_culled = true;
        state->cull_count++;
    }
    
    return state->should_render;
}

void pass_rendered(pass_cull_state_t *state, double wall_time) {
    if (!state) return;
    state->last_render_time = wall_time;
    state->render_count++;
}

void pass_culled(pass_cull_state_t *state, int reason) {
    if (!state) return;
    state->was_culled = true;
    state->cull_reason = reason;
    state->cull_count++;
}

/* ============================================================================
 * Temporal Optimization Functions
 * ============================================================================ */

void temporal_init(temporal_state_t *state, temporal_mode_t mode) {
    if (!state) return;
    
    memset(state, 0, sizeof(*state));
    state->mode = mode;
    state->max_consecutive_skips = 4;  /* Don't skip more than 4 frames in a row */
}

void temporal_destroy(temporal_state_t *state) {
    if (!state) return;
    
    /* Free GL resources if allocated */
    if (state->accumulation_texture) {
        glDeleteTextures(1, &state->accumulation_texture);
    }
    if (state->prev_frame_texture) {
        glDeleteTextures(1, &state->prev_frame_texture);
    }
    if (state->motion_vectors) {
        glDeleteTextures(1, &state->motion_vectors);
    }
    if (state->checkerboard_stencil) {
        glDeleteTextures(1, &state->checkerboard_stencil);
    }
    
    memset(state, 0, sizeof(*state));
}

void temporal_update(temporal_state_t *state,
                     float time, float mouse_x, float mouse_y, bool mouse_click,
                     double wall_time) {
    if (!state) return;
    
    state->skip_this_frame = false;
    state->reuse_previous = false;
    
    /* Add to history */
    int idx = state->history_index;
    state->history[idx].time = time;
    state->history[idx].mouse_x = mouse_x;
    state->history[idx].mouse_y = mouse_y;
    state->history[idx].mouse_click = mouse_click;
    state->history[idx].wall_time = wall_time;
    state->history[idx].frame_hash = opt_hash_combine(
        opt_hash_float(time),
        opt_hash_combine(opt_hash_float(mouse_x), opt_hash_float(mouse_y))
    );
    
    state->history_index = (idx + 1) & (OPT_TEMPORAL_HISTORY - 1);
    if (state->history_count < OPT_TEMPORAL_HISTORY) {
        state->history_count++;
    }
    
    /* Analyze motion */
    if (state->history_count >= 2) {
        int prev_idx = (idx - 1 + OPT_TEMPORAL_HISTORY) & (OPT_TEMPORAL_HISTORY - 1);
        float time_delta = fabsf(time - state->history[prev_idx].time);
        float mouse_delta = fabsf(mouse_x - state->history[prev_idx].mouse_x) +
                           fabsf(mouse_y - state->history[prev_idx].mouse_y);
        
        state->motion_estimate = time_delta * 10.0f + mouse_delta * 0.1f;
        
        /* Detect static frames */
        if (time_delta < 0.0001f && mouse_delta < 0.1f) {
            state->static_frames++;
        } else {
            state->static_frames = 0;
        }
    }
    
    /* Make skip/reuse decision based on mode */
    switch (state->mode) {
        case TEMPORAL_MODE_NONE:
            /* Never skip */
            break;
            
        case TEMPORAL_MODE_ACCUMULATE:
            /* Reuse if scene is static for multiple frames */
            if (state->static_frames > 2 && 
                state->consecutive_skips < state->max_consecutive_skips) {
                state->reuse_previous = true;
                state->consecutive_skips++;
            } else {
                state->consecutive_skips = 0;
            }
            break;
            
        case TEMPORAL_MODE_CHECKERBOARD:
            /* Alternate phases */
            state->checkerboard_phase = (state->checkerboard_phase + 1) & 1;
            break;
            
        case TEMPORAL_MODE_INTERPOLATE:
            /* Skip every other frame if motion is low */
            if (state->motion_estimate < 0.1f &&
                state->consecutive_skips < state->max_consecutive_skips) {
                state->skip_this_frame = (state->history_count & 1) == 0;
                if (state->skip_this_frame) {
                    state->consecutive_skips++;
                    state->interpolation_factor = 0.5f;
                } else {
                    state->consecutive_skips = 0;
                }
            }
            break;
            
        case TEMPORAL_MODE_AUTO:
            /* Use accumulation for very static scenes */
            if (state->static_frames > 4 &&
                state->consecutive_skips < state->max_consecutive_skips) {
                state->reuse_previous = true;
                state->consecutive_skips++;
            } else {
                state->consecutive_skips = 0;
            }
            break;
    }
}

bool temporal_should_skip(const temporal_state_t *state) {
    return state ? state->skip_this_frame : false;
}

bool temporal_should_reuse(const temporal_state_t *state) {
    return state ? state->reuse_previous : false;
}

int temporal_get_checkerboard_phase(const temporal_state_t *state) {
    return state ? state->checkerboard_phase : 0;
}

float temporal_get_interpolation_factor(const temporal_state_t *state) {
    return state ? state->interpolation_factor : 0.0f;
}

void temporal_frame_rendered(temporal_state_t *state, double wall_time) {
    (void)wall_time;  /* Reserved for future timestamp tracking */
    
    if (!state) return;
    /* Reset skip counter since we rendered */
    state->consecutive_skips = 0;
}

/* ============================================================================
 * Statistics and Debugging
 * ============================================================================ */

render_optimizer_stats_t render_optimizer_get_stats(const render_optimizer_t *opt) {
    render_optimizer_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    
    if (!opt) return stats;
    
    /* Calculate efficiency metrics */
    if (opt->stats.gl_calls_total > 0) {
        stats.gl_call_efficiency = (float)opt->stats.gl_calls_avoided / 
                                   (float)(opt->stats.gl_calls_total + opt->stats.gl_calls_avoided) * 100.0f;
    }
    
    if (opt->stats.uniform_updates_total > 0) {
        stats.uniform_efficiency = (float)opt->stats.uniform_updates_avoided /
                                   (float)opt->stats.uniform_updates_total * 100.0f;
    }
    
    if (opt->stats.texture_binds_total > 0) {
        stats.texture_bind_efficiency = (float)opt->stats.texture_binds_avoided /
                                        (float)opt->stats.texture_binds_total * 100.0f;
    }
    
    if (opt->stats.fbo_binds_total > 0) {
        stats.fbo_bind_efficiency = (float)opt->stats.fbo_binds_avoided /
                                    (float)opt->stats.fbo_binds_total * 100.0f;
    }
    
    if (opt->stats.program_switches_total > 0) {
        stats.program_switch_efficiency = (float)opt->stats.program_switches_avoided /
                                          (float)opt->stats.program_switches_total * 100.0f;
    }
    
    /* Pass metrics */
    uint64_t total_passes = opt->stats.passes_rendered + opt->stats.passes_culled;
    if (total_passes > 0) {
        stats.pass_cull_rate = (float)opt->stats.passes_culled / (float)total_passes * 100.0f;
    }
    
    uint64_t total_frames = opt->stats.passes_rendered + opt->stats.passes_reused;
    if (total_frames > 0) {
        stats.temporal_reuse_rate = (float)opt->stats.passes_reused / (float)total_frames * 100.0f;
    }
    
    /* Estimate speedup */
    float call_savings = stats.gl_call_efficiency / 100.0f * 0.1f;       /* ~10% of time is API calls */
    float uniform_savings = stats.uniform_efficiency / 100.0f * 0.05f;  /* ~5% is uniform updates */
    float cull_savings = stats.pass_cull_rate / 100.0f * 0.3f;          /* ~30% if passes culled */
    float temporal_savings = stats.temporal_reuse_rate / 100.0f * 0.4f; /* ~40% if frames reused */
    
    float total_savings = call_savings + uniform_savings + cull_savings + temporal_savings;
    stats.estimated_speedup = 1.0f / (1.0f - total_savings);
    if (stats.estimated_speedup < 1.0f) stats.estimated_speedup = 1.0f;
    if (stats.estimated_speedup > 4.0f) stats.estimated_speedup = 4.0f;  /* Cap at 4x */
    
    /* Current state */
    stats.frame_number = opt->frame_number;
    stats.frame_time_ms = opt->frame_time_ms;
    stats.mouse_idle_seconds = opt->mouse_idle_seconds;
    
    return stats;
}

void render_optimizer_reset_stats(render_optimizer_t *opt) {
    if (opt) {
        memset(&opt->stats, 0, sizeof(opt->stats));
    }
}

void render_optimizer_log_stats(const render_optimizer_t *opt) {
    if (!opt) return;
    
    render_optimizer_stats_t stats = render_optimizer_get_stats(opt);
    
    log_info("=== Render Optimizer Stats (Frame %lu) ===", (unsigned long)stats.frame_number);
    log_info("  GL call efficiency:     %.1f%% (%lu avoided)",
             stats.gl_call_efficiency, (unsigned long)opt->stats.gl_calls_avoided);
    log_info("  Uniform efficiency:     %.1f%% (%lu avoided)",
             stats.uniform_efficiency, (unsigned long)opt->stats.uniform_updates_avoided);
    log_info("  Texture bind efficiency: %.1f%% (%lu avoided)",
             stats.texture_bind_efficiency, (unsigned long)opt->stats.texture_binds_avoided);
    log_info("  FBO bind efficiency:    %.1f%% (%lu avoided)",
             stats.fbo_bind_efficiency, (unsigned long)opt->stats.fbo_binds_avoided);
    log_info("  Program switch efficiency: %.1f%% (%lu avoided)",
             stats.program_switch_efficiency, (unsigned long)opt->stats.program_switches_avoided);
    log_info("  Pass cull rate:         %.1f%%", stats.pass_cull_rate);
    log_info("  Temporal reuse rate:    %.1f%%", stats.temporal_reuse_rate);
    log_info("  Estimated speedup:      %.2fx", stats.estimated_speedup);
    log_info("  Frame time:             %.2f ms", stats.frame_time_ms);
}

void render_optimizer_sync_state(render_optimizer_t *opt) {
    if (!opt) return;
    
    /* Query actual GPU state and sync cache */
    GLint val;
    
    glGetIntegerv(GL_CURRENT_PROGRAM, &val);
    opt->state.current_program = (GLuint)val;
    
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &val);
    opt->state.current_vao = (GLuint)val;
    
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &val);
    opt->state.current_vbo = (GLuint)val;
    
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &val);
    opt->state.current_fbo = (GLuint)val;
    
    glGetIntegerv(GL_ACTIVE_TEXTURE, &val);
    opt->state.active_texture_unit = (GLenum)val;
    
    opt->state.depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
    opt->state.blend_enabled = glIsEnabled(GL_BLEND);
    opt->state.cull_face_enabled = glIsEnabled(GL_CULL_FACE);
    opt->state.scissor_test_enabled = glIsEnabled(GL_SCISSOR_TEST);
    
    glGetIntegerv(GL_VIEWPORT, opt->state.viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, opt->state.clear_color);
    
    log_info("Render optimizer state synced with GPU");
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

uint64_t opt_hash_floats(const float *values, int count) {
    if (!values || count <= 0) return 0;
    
    uint64_t hash = 14695981039346656037ULL;  /* FNV offset basis */
    for (int i = 0; i < count; i++) {
        hash = opt_hash_combine(hash, opt_hash_float(values[i]));
    }
    return hash;
}

uint64_t opt_hash_combine(uint64_t h1, uint64_t h2) {
    /* Boost-style hash combine */
    return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
}

bool opt_floats_equal(const float *a, const float *b, int count, float epsilon) {
    if (!a || !b || count <= 0) return false;
    
    for (int i = 0; i < count; i++) {
        if (!opt_float_eq(a[i], b[i], epsilon)) {
            return false;
        }
    }
    return true;
}

float buffer_hint_to_scale(buffer_resolution_hint_t hint) {
    switch (hint) {
        case BUFFER_HINT_FULL:   return 1.0f;
        case BUFFER_HINT_HIGH:   return 0.75f;
        case BUFFER_HINT_MEDIUM: return 0.5f;
        case BUFFER_HINT_LOW:    return 0.25f;
        case BUFFER_HINT_TINY:   return 0.125f;
        case BUFFER_HINT_AUTO:   return 0.75f;  /* Default to high */
        default:                 return 1.0f;
    }
}

buffer_resolution_hint_t scale_to_buffer_hint(float scale) {
    if (scale >= 0.9f)  return BUFFER_HINT_FULL;
    if (scale >= 0.6f)  return BUFFER_HINT_HIGH;
    if (scale >= 0.4f)  return BUFFER_HINT_MEDIUM;
    if (scale >= 0.2f)  return BUFFER_HINT_LOW;
    return BUFFER_HINT_TINY;
}
