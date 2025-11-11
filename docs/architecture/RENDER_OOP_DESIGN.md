# Render Module OOP Design

## Current Problems

1. **Tight Coupling**: `struct output_state` contains 20+ OpenGL-specific fields
2. **Leaky Abstraction**: Output module must know about VBOs, shader programs, uniform locations
3. **Poor Encapsulation**: GL state scattered across output, can't easily mock for testing
4. **Code Smell**: Output doing both business logic AND managing GL implementation details

## Proposed C-Style OOP Design

### 1. Opaque Context Type

```c
/* render.h - Public interface */
typedef struct render_context render_context_t;

/* render.c - Private implementation */
struct render_context {
    /* Dimensions */
    int32_t width;
    int32_t height;
    
    /* Time state for shader animation */
    float time_sec;
    float last_frame_time;
    
    /* OpenGL resources */
    GLuint vbo;                    // Vertex buffer object
    GLuint program;                // Default image shader
    GLuint glitch_program;         // Transition shaders
    GLuint pixelate_program;
    GLuint live_shader_program;    // Active live shader
    
    /* Cached uniform locations */
    struct {
        GLint position;
        GLint texcoord;
        GLint tex_sampler;
        GLint u_resolution;
        GLint u_time;
        GLint u_speed;
        GLint *iChannel;           // Dynamic array
    } shader_uniforms;
    
    struct {
        GLint position;
        GLint texcoord;
        GLint tex_sampler;
    } program_uniforms;
    
    struct {
        GLint position;
        GLint texcoord;
        GLint tex0;
        GLint tex1;
        GLint progress;
    } transition_uniforms;
    
    /* iChannel textures */
    GLuint *channel_textures;      // Dynamic array
    size_t channel_count;
    
    /* Back-reference to global state (for GL capabilities) */
    struct neowall_state *state;
};
```

### 2. Constructor/Destructor Pattern

```c
/* Creation - allocates and initializes all GL resources */
render_context_t *render_context_create(struct neowall_state *state, 
                                        int32_t width, int32_t height) {
    render_context_t *ctx = calloc(1, sizeof(render_context_t));
    if (!ctx) return NULL;
    
    ctx->state = state;
    ctx->width = width;
    ctx->height = height;
    
    /* Initialize VBO */
    glGenBuffers(1, &ctx->vbo);
    
    /* Create default shader programs */
    if (!create_default_programs(ctx)) {
        render_context_destroy(ctx);
        return NULL;
    }
    
    return ctx;
}

/* Destruction - cleans up all GL resources */
void render_context_destroy(render_context_t *ctx) {
    if (!ctx) return;
    
    /* Clean up shaders */
    if (ctx->program) shader_destroy_program(ctx->program);
    if (ctx->glitch_program) shader_destroy_program(ctx->glitch_program);
    if (ctx->pixelate_program) shader_destroy_program(ctx->pixelate_program);
    if (ctx->live_shader_program) shader_destroy_program(ctx->live_shader_program);
    
    /* Clean up channel textures */
    render_cleanup_channel_textures(ctx);
    
    /* Clean up VBO */
    if (ctx->vbo) glDeleteBuffers(1, &ctx->vbo);
    
    free(ctx);
}
```

### 3. Setter Methods for State

```c
/* Setters - update render context state */
void render_context_set_dimensions(render_context_t *ctx, int32_t width, int32_t height) {
    ctx->width = width;
    ctx->height = height;
}

void render_context_set_time(render_context_t *ctx, float time_sec) {
    ctx->time_sec = time_sec;
}
```

### 4. Encapsulated Rendering Operations

```c
/* Render an image with specified mode */
bool render_frame_image(render_context_t *ctx, GLuint texture, 
                       enum wallpaper_mode mode) {
    if (!ctx || !texture) return false;
    
    glUseProgram(ctx->program);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
    
    /* Set up vertex attributes */
    glVertexAttribPointer(ctx->program_uniforms.position, 2, GL_FLOAT, GL_FALSE, 
                         4 * sizeof(float), (void*)0);
    glVertexAttribPointer(ctx->program_uniforms.texcoord, 2, GL_FLOAT, GL_FALSE,
                         4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glEnableVertexAttribArray(ctx->program_uniforms.position);
    glEnableVertexAttribArray(ctx->program_uniforms.texcoord);
    
    /* Bind texture */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(ctx->program_uniforms.tex_sampler, 0);
    
    /* Calculate geometry based on mode */
    calculate_geometry_for_mode(ctx, mode);
    
    /* Draw */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    return true;
}

/* Render a shader-based wallpaper */
bool render_frame_shader(render_context_t *ctx, struct wallpaper_config *config) {
    if (!ctx || !ctx->live_shader_program) return false;
    
    glUseProgram(ctx->live_shader_program);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
    
    /* Set uniforms */
    glUniform2f(ctx->shader_uniforms.u_resolution, 
               (float)ctx->width, (float)ctx->height);
    glUniform1f(ctx->shader_uniforms.u_time, ctx->time_sec);
    glUniform1f(ctx->shader_uniforms.u_speed, config->shader_speed);
    
    /* Bind iChannel textures */
    for (size_t i = 0; i < ctx->channel_count; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, ctx->channel_textures[i]);
        glUniform1i(ctx->shader_uniforms.iChannel[i], i);
    }
    
    /* Set up vertex attributes and draw */
    setup_vertex_attributes(ctx);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    return true;
}

/* Render a transition between two textures */
bool render_frame_transition(render_context_t *ctx, enum transition_type type,
                             GLuint from_texture, GLuint to_texture, 
                             float progress) {
    if (!ctx) return false;
    
    GLuint program = select_transition_program(ctx, type);
    if (!program) return render_frame_image(ctx, to_texture, MODE_FIT);
    
    glUseProgram(program);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
    
    /* Bind both textures */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, from_texture);
    glUniform1i(ctx->transition_uniforms.tex0, 0);
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, to_texture);
    glUniform1i(ctx->transition_uniforms.tex1, 1);
    
    /* Set progress uniform */
    glUniform1f(ctx->transition_uniforms.progress, progress);
    
    /* Draw */
    setup_vertex_attributes(ctx);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    return true;
}
```

### 5. Channel Texture Management

```c
/* Load channel textures from config */
bool render_load_channel_textures(render_context_t *ctx, 
                                  struct wallpaper_config *config) {
    if (!ctx || !config) return false;
    
    /* Clean up existing channels */
    render_cleanup_channel_textures(ctx);
    
    /* Allocate new channel array */
    if (config->channel_count > 0) {
        ctx->channel_textures = calloc(config->channel_count, sizeof(GLuint));
        ctx->channel_count = config->channel_count;
        
        /* Load each channel */
        for (size_t i = 0; i < config->channel_count; i++) {
            struct image_data *img = image_load(config->channel_paths[i], 
                                               ctx->width, ctx->height, MODE_FIT);
            if (img) {
                ctx->channel_textures[i] = render_create_texture_flipped(img);
                image_free(img);
            }
        }
    }
    
    return true;
}

/* Update a single channel texture */
bool render_update_channel_texture(render_context_t *ctx, 
                                   size_t channel_index, 
                                   const char *image_path) {
    if (!ctx || channel_index >= ctx->channel_count) return false;
    
    /* Destroy old texture */
    if (ctx->channel_textures[channel_index]) {
        render_destroy_texture(ctx->channel_textures[channel_index]);
    }
    
    /* Load new texture */
    struct image_data *img = image_load(image_path, ctx->width, ctx->height, MODE_FIT);
    if (!img) return false;
    
    ctx->channel_textures[channel_index] = render_create_texture_flipped(img);
    image_free(img);
    
    return true;
}

/* Get channel textures (for external use) */
GLuint *render_get_channel_textures(render_context_t *ctx, size_t *out_count) {
    if (!ctx) return NULL;
    if (out_count) *out_count = ctx->channel_count;
    return ctx->channel_textures;
}

/* Clean up all channel textures */
void render_cleanup_channel_textures(render_context_t *ctx) {
    if (!ctx) return;
    
    for (size_t i = 0; i < ctx->channel_count; i++) {
        if (ctx->channel_textures[i]) {
            render_destroy_texture(ctx->channel_textures[i]);
        }
    }
    
    free(ctx->channel_textures);
    ctx->channel_textures = NULL;
    ctx->channel_count = 0;
}
```

### 6. Shader Management

```c
/* Load a live shader */
bool render_load_shader(render_context_t *ctx, struct neowall_state *state,
                       const char *shader_path, float speed) {
    if (!ctx) return false;
    
    /* Clean up old shader */
    if (ctx->live_shader_program) {
        shader_destroy_program(ctx->live_shader_program);
        ctx->live_shader_program = 0;
    }
    
    /* Compile new shader */
    ctx->live_shader_program = compile_shadertoy_shader(state, shader_path, speed);
    if (!ctx->live_shader_program) {
        return false;
    }
    
    /* Cache uniform locations */
    cache_shader_uniforms(ctx);
    
    return true;
}

/* Clean up all shaders */
void render_cleanup_shaders(render_context_t *ctx) {
    if (!ctx) return;
    
    if (ctx->live_shader_program) {
        shader_destroy_program(ctx->live_shader_program);
        ctx->live_shader_program = 0;
    }
}

/* Get current live shader */
GLuint render_get_live_shader(render_context_t *ctx) {
    return ctx ? ctx->live_shader_program : 0;
}
```

### 7. Updated output_state Structure

```c
/* output.h - Clean separation */
struct output_state {
    /* Wayland/compositor state */
    struct wl_output *output;
    struct zxdg_output_v1 *xdg_output;
    struct compositor_surface *compositor_surface;
    
    /* Output properties */
    uint32_t name;
    int32_t width, height;
    int32_t scale, transform;
    char make[64], model[64], connector_name[64];
    
    /* Wallpaper state (business logic) */
    struct wallpaper_config *config;
    struct image_data *current_image;
    struct image_data *next_image;
    GLuint texture;
    GLuint next_texture;
    
    /* Preloading state */
    GLuint preload_texture;
    struct image_data *preload_image;
    pthread_t preload_thread;
    atomic_bool_t preload_ready;
    
    /* Transition state */
    enum transition_type transition_type;
    float transition_progress;
    uint64_t transition_start_time;
    
    /* Render context - OPAQUE POINTER */
    render_context_t *render_ctx;
    
    /* Back-references */
    struct neowall_state *state;
    struct output_state *next;
};
```

### 8. Updated output module usage

```c
/* output.c - Uses render context through clean API */

bool output_create_egl_surface(struct output_state *output) {
    /* ... EGL setup ... */
    
    /* Create render context */
    output->render_ctx = render_context_create(output->state, 
                                               output->width, 
                                               output->height);
    if (!output->render_ctx) {
        log_error("Failed to create render context");
        return false;
    }
    
    return true;
}

void output_destroy(struct output_state *output) {
    /* Clean up render context */
    if (output->render_ctx) {
        render_context_destroy(output->render_ctx);
        output->render_ctx = NULL;
    }
    
    /* ... rest of cleanup ... */
}

bool output_render_frame(struct output_state *output) {
    if (!output->render_ctx) return false;
    
    /* Update time for shader animation */
    float current_time = get_time_ms() / 1000.0f;
    render_context_set_time(output->render_ctx, current_time);
    
    /* Handle different rendering modes */
    if (output->config->type == WALLPAPER_SHADER) {
        return render_frame_shader(output->render_ctx, output->config);
    }
    
    /* Transition rendering */
    if (output->transition_progress > 0.0f && output->transition_progress < 1.0f) {
        return render_frame_transition(output->render_ctx,
                                       output->config->transition,
                                       output->texture,
                                       output->next_texture,
                                       output->transition_progress);
    }
    
    /* Normal image rendering */
    return render_frame_image(output->render_ctx, 
                             output->texture,
                             output->config->mode);
}
```

## Benefits of This Design

### 1. **True Encapsulation**
- Output module never touches GL state directly
- All GL code is in render.c
- Easy to mock render context for testing

### 2. **Clear Ownership**
- Render context owns ALL GL resources
- Output module owns business logic
- No shared mutable state

### 3. **Type Safety**
- `render_context_t` is opaque - can't accidentally access internals
- Compiler enforces clean API boundaries

### 4. **Testability**
```c
/* Can easily create mock render context */
render_context_t *mock_render_context_create() {
    /* Return stub that records calls */
}

/* Test output logic without OpenGL */
void test_output_cycle() {
    output->render_ctx = mock_render_context_create();
    output_cycle_wallpaper(output);
    assert(mock_was_called("render_frame_transition"));
}
```

### 5. **Maintainability**
- Change GL implementation without touching output module
- Could even swap GLES for Vulkan by reimplementing render_context
- Clean separation of concerns

## Migration Path

### Phase 1: Create Context Type (Non-Breaking)
1. Define `struct render_context` in render.c
2. Keep all current functions working
3. Add new OOP functions alongside old ones

### Phase 2: Update Output Module
1. Add `render_ctx` field to `output_state`
2. Create context in `output_create_egl_surface()`
3. Replace direct GL access with context calls

### Phase 3: Remove Old API
1. Delete GL fields from `output_state`
2. Remove old render functions
3. Update all call sites

### Phase 4: Polish
1. Add documentation
2. Write tests
3. Optimize performance

## Comparison with Current Design

| Aspect | Current | OOP Design |
|--------|---------|-----------|
| GL state location | `output_state` | `render_context` (opaque) |
| Coupling | Tight (output knows GL) | Loose (output uses API) |
| Testability | Hard (need real GL) | Easy (can mock context) |
| Code clarity | Mixed concerns | Clear separation |
| Lines of code | ~1500 | ~1800 (but cleaner) |

## Conclusion

This OOP design provides:
- ✅ Better encapsulation
- ✅ Clearer ownership
- ✅ Easier testing
- ✅ More maintainable code
- ✅ Type-safe API
- ✅ Follows C best practices

The trade-off is slightly more code, but the benefits in maintainability and clarity are worth it for a codebase of this size.
