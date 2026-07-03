/* Shader Multipass Support
 * Implements Shadertoy-style multipass rendering with BufferA-D and Image passes
 * 
 * Shadertoy multipass structure:
 * - BufferA, BufferB, BufferC, BufferD: Intermediate render targets
 * - Image: Final output pass
 * - Each buffer can read from any other buffer via iChannel0-3
 * - Buffers can self-reference for feedback effects (ping-pong rendering)
 */

#ifndef SHADER_MULTIPASS_H
#define SHADER_MULTIPASS_H

#include <stdbool.h>
#include <stddef.h>
#include "neowall/shader/platform_compat.h"
#include "neowall/shader/adaptive_scale.h"
#include "neowall/shader/render_optimizer.h"
#include "neowall/shader/multipass_optimizer.h"
#include "neowall/shader/reactive.h"

/* Maximum number of passes supported (BufferA-D + Image) */
#define MULTIPASS_MAX_BUFFERS 4
#define MULTIPASS_MAX_PASSES  5
#define MULTIPASS_MAX_CHANNELS 4

/* Pass types matching Shadertoy */
typedef enum {
    PASS_TYPE_NONE = 0,
    PASS_TYPE_BUFFER_A,
    PASS_TYPE_BUFFER_B,
    PASS_TYPE_BUFFER_C,
    PASS_TYPE_BUFFER_D,
    PASS_TYPE_IMAGE,
    PASS_TYPE_COMMON,      /* Common code included in all passes */
    PASS_TYPE_SOUND        /* Audio pass (not implemented yet) */
} multipass_type_t;

/* Channel input source */
typedef enum {
    CHANNEL_SOURCE_NONE = 0,
    CHANNEL_SOURCE_BUFFER_A,
    CHANNEL_SOURCE_BUFFER_B,
    CHANNEL_SOURCE_BUFFER_C,
    CHANNEL_SOURCE_BUFFER_D,
    CHANNEL_SOURCE_TEXTURE,    /* External texture */
    CHANNEL_SOURCE_KEYBOARD,   /* Keyboard input texture */
    CHANNEL_SOURCE_NOISE,      /* Procedural noise */
    CHANNEL_SOURCE_SELF,       /* Self-reference (previous frame) */
    CHANNEL_SOURCE_AUDIO       /* Live audio: row0 spectrum, row1 waveform */
} channel_source_t;

/* Channel configuration */
typedef struct {
    channel_source_t source;
    int texture_id;            /* For CHANNEL_SOURCE_TEXTURE */
    bool vflip;                /* Vertical flip */
    int filter;                /* GL_LINEAR or GL_NEAREST */
    int wrap;                  /* GL_REPEAT, GL_CLAMP_TO_EDGE, etc. */
} multipass_channel_t;

/* A user-defined uniform supplied by a .neowall manifest. The value is either a
 * literal (set once) or bound to a live reactive signal that updates per frame. */
#define MULTIPASS_MAX_USER_UNIFORMS 16
#define MULTIPASS_UNIFORM_NAME_MAX 48

typedef enum {
    UNIFORM_BIND_CONST = 0,  /* fixed value from manifest */
    UNIFORM_BIND_CPU,
    UNIFORM_BIND_RAM,
    UNIFORM_BIND_NET_DOWN,
    UNIFORM_BIND_NET_UP,
    UNIFORM_BIND_BATTERY,
    UNIFORM_BIND_TIME_OF_DAY,
    UNIFORM_BIND_SUN,
    UNIFORM_BIND_AUDIO_LEVEL,
    UNIFORM_BIND_AUDIO_BASS,
    UNIFORM_BIND_AUDIO_MID,
    UNIFORM_BIND_AUDIO_TREBLE,
    UNIFORM_BIND_AUDIO_BEAT,
    UNIFORM_BIND_KEY_ENERGY,
    UNIFORM_BIND_MOUSE_ENERGY,
    UNIFORM_BIND_SWAP,
    UNIFORM_BIND_DISK_READ,
    UNIFORM_BIND_DISK_WRITE,
    UNIFORM_BIND_LOAD,
    UNIFORM_BIND_CPU_TEMP,
    UNIFORM_BIND_GPU,
    UNIFORM_BIND_GPU_TEMP,
    UNIFORM_BIND_UPTIME,
    UNIFORM_BIND_PROCS
} uniform_bind_t;

typedef struct {
    char name[MULTIPASS_UNIFORM_NAME_MAX]; /* GLSL uniform name (float) */
    uniform_bind_t bind;                   /* live source, or CONST */
    float value;                           /* current/constant value */
    GLint location;                        /* cached per pass at compile (-1) */
} multipass_user_uniform_t;

/* Cached uniform locations for performance (avoid glGetUniformLocation every frame) */
typedef struct {
    GLint iTime;
    GLint iTimeDelta;
    GLint iFrameRate;
    GLint iFrame;
    GLint iResolution;
    GLint iMouse;
    GLint iDate;
    GLint iSampleRate;
    GLint iChannelResolution;
    GLint iChannel[MULTIPASS_MAX_CHANNELS];
    /* neowall reactive uniforms (see shader_stdlib.h). -1 if shader unused. */
    GLint iCpu, iCpuCores, iCpuCoreCount, iRam, iNetDown, iNetUp;
    GLint iCpuMax, iCpuSpread, iRamGB, iRamTotalGB, iNetDownRaw, iNetUpRaw;
    GLint iSwap, iDiskRead, iDiskWrite, iLoad, iLoadRaw;
    GLint iCpuTemp, iCpuTempC, iGpu, iGpuTemp, iGpuTempC;
    GLint iNvGpu, iNvVram, iNvGpuTempC, iNvPower, iNvActive;
    GLint iThermal, iActivity, iPulse;
    GLint iUptimeHours, iProcs, iProcCount;
    GLint iBattery, iCharging, iTimeOfDay, iSun, iDayFraction;
    GLint iKeyEnergy, iMouseEnergy;
    GLint iAudioLevel, iAudioBass, iAudioMid, iAudioTreble, iAudioBeat, iAudioActive;
    GLint iAudio;               /* audio spectrum/waveform sampler */
    bool cached;                /* True if locations have been cached */
} uniform_locations_t;

/* Single pass configuration */
typedef struct {
    multipass_type_t type;
    char *name;                              /* Pass name (e.g., "Buffer A") */
    char *source;                            /* GLSL source code for this pass */
    multipass_channel_t channels[MULTIPASS_MAX_CHANNELS];
    GLuint program;                          /* Compiled shader program */
    GLuint fbo;                              /* Framebuffer object (NULL for Image pass) */
    GLuint textures[2];                      /* Ping-pong textures for feedback */
    int ping_pong_index;                     /* Current read texture index */
    int width;                               /* Render target width */
    int height;                              /* Render target height */
    bool needs_clear;                        /* Clear buffer on first frame */
    bool is_compiled;                        /* Compilation status */
    char *compile_error;                     /* Compilation error message */
    uniform_locations_t uniforms;            /* Cached uniform locations */
    bool needs_mipmaps;                      /* True if shader uses textureLod */
    int channel_buffer_index[MULTIPASS_MAX_CHANNELS]; /* Cached buffer pass indices for channels (-1 if not a buffer) */
} multipass_pass_t;

/* Complete multipass shader configuration */
/* Complete multipass shader configuration */
typedef struct {
    char *common_source;                     /* Common code shared by all passes */
    multipass_pass_t passes[MULTIPASS_MAX_PASSES];
    int pass_count;                          /* Number of active passes */
    int image_pass_index;                    /* Index of the Image pass (-1 if none) */
    bool has_buffers;                        /* True if any buffer passes exist */
    int frame_count;                         /* Frame counter for iFrame uniform */
    double start_time;                       /* Start time for iTime uniform */
    
    /* Shared resources */
    GLuint vao;                              /* Vertex array object */
    GLuint vbo;                              /* Vertex buffer for fullscreen quad */
    GLuint noise_texture;                    /* Default noise texture */
    GLuint keyboard_texture;                 /* Keyboard state texture */
    GLuint audio_texture;                    /* Live audio (512x2) for iAudio / CHANNEL_SOURCE_AUDIO */
    GLint default_framebuffer;               /* Default framebuffer ID (may not be 0 in GTK) */
    
    /* Resolution scaling */
    float resolution_scale;                  /* Current buffer resolution scale */
    float min_resolution_scale;              /* Minimum allowed scale */
    float max_resolution_scale;              /* Maximum allowed scale */
    int scaled_width;                        /* Cached scaled width */
    int scaled_height;                       /* Cached scaled height */
    
    /* Industry-grade adaptive resolution scaling */
    adaptive_state_t adaptive;
    
    /* GPU state optimization */
    render_optimizer_t optimizer;
    
    /* Smart multipass optimization (per-buffer resolution, half-rate updates) */
    multipass_optimizer_t multipass_opt;
    
    /* Per-buffer resolution analysis (legacy - use multipass_opt instead) */
    buffer_analysis_t buffer_analysis[MULTIPASS_MAX_BUFFERS];
    bool use_smart_buffer_sizing;            /* Auto-detect optimal buffer resolutions */
    
    bool is_initialized;                     /* OpenGL resources initialized */
    bool is_animated;                        /* False = no time/mouse/audio refs: render once, then sleep */

    /* Per-frame cache: computed ONCE in multipass_render, consumed by
     * multipass_set_uniforms for every pass. Avoids redundant mutex-guarded
     * reactive snapshots and time()/localtime() syscalls per pass. */
    double last_frame_wall;                  /* monotonic seconds at previous frame (0 = first) */
    float frame_dt;                          /* real seconds between frames, clamped sane */
    float frame_fps;                         /* smoothed instantaneous FPS for iFrameRate */
    float date_cached[4];                    /* iDate vec4, refreshed when the second ticks */
    long long date_cached_sec;               /* time() value date_cached was built for */
    reactive_snapshot_t frame_reactive;      /* one reactive snapshot per frame */

    /* User uniforms declared by a .neowall manifest (Tier 2/3). Declared into
     * the wrapper at compile time and set each frame in multipass_set_uniforms. */
    multipass_user_uniform_t user_uniforms[MULTIPASS_MAX_USER_UNIFORMS];
    int user_uniform_count;
    bool explicit_bindings;                  /* true if channels came from a manifest */
} multipass_shader_t;

/* Parse result for shader analysis */
typedef struct {
    bool is_multipass;                       /* True if multiple passes detected */
    int pass_count;                          /* Number of passes found */
    char *pass_sources[MULTIPASS_MAX_PASSES]; /* Extracted source for each pass */
    multipass_type_t pass_types[MULTIPASS_MAX_PASSES];
    char *common_source;                     /* Common code (if any) */
    char *error_message;                     /* Parse error (if any) */
} multipass_parse_result_t;

/* ============================================
 * Shader Parsing Functions
 * ============================================ */

/**
 * Parse a shader source to detect and extract multipass structure
 * 
 * Detects multiple mainImage functions or Shadertoy-style pass markers:
 * - Multiple "void mainImage(" definitions
 * - Comments like "// Buffer A" or "// Image"
 * - Shadertoy JSON-style markers (if present)
 * 
 * @param source Complete shader source code
 * @return Parse result (caller must free with multipass_free_parse_result)
 */
multipass_parse_result_t *multipass_parse_shader(const char *source);

/**
 * Free parse result
 * 
 * @param result Parse result to free
 */
void multipass_free_parse_result(multipass_parse_result_t *result);

/**
 * Detect if shader is multipass based on heuristics
 * Quick check without full parsing
 * 
 * @param source Shader source code
 * @return true if shader appears to be multipass
 */
bool multipass_detect(const char *source);

/**
 * Count number of mainImage functions in source
 * 
 * @param source Shader source code
 * @return Number of mainImage functions found
 */
int multipass_count_main_functions(const char *source);

/**
 * Extract common code section from shader
 * Common code is included before all pass-specific code
 * 
 * @param source Shader source code
 * @return Common code section (caller must free) or NULL
 */
char *multipass_extract_common(const char *source);

/* ============================================
 * Shader Compilation Functions
 * ============================================ */

/**
 * Create a new multipass shader from source
 * Parses, compiles, and sets up all passes
 * 
 * @param source Complete shader source code
 * @return New multipass shader (caller must free with multipass_destroy)
 */
multipass_shader_t *multipass_create(const char *source);

/**
 * Create multipass shader from parse result
 * Useful when you've already parsed the shader
 * 
 * @param parse_result Previously parsed shader
 * @return New multipass shader (caller must free with multipass_destroy)
 */
multipass_shader_t *multipass_create_from_parsed(const multipass_parse_result_t *parse_result);

/**
 * Initialize OpenGL resources for multipass shader
 * Must be called with valid OpenGL context
 * 
 * @param shader Multipass shader to initialize
 * @param width Initial render target width
 * @param height Initial render target height
 * @return true on success, false on failure
 */
bool multipass_init_gl(multipass_shader_t *shader, int width, int height);

/**
 * Compile a single pass
 * 
 * @param shader Parent multipass shader
 * @param pass_index Index of pass to compile
 * @return true on success, false on failure
 */
bool multipass_compile_pass(multipass_shader_t *shader, int pass_index);

/**
 * Compile all passes
 * 
 * @param shader Multipass shader
 * @return true if all passes compiled successfully
 */
bool multipass_compile_all(multipass_shader_t *shader);

/**
 * Resize render targets
 * Called when window size changes
 * 
 * @param shader Multipass shader
 * @param width New width
 * @param height New height
 */
void multipass_resize(multipass_shader_t *shader, int width, int height);

/**
 * Destroy multipass shader and free all resources
 * 
 * @param shader Shader to destroy
 */
void multipass_destroy(multipass_shader_t *shader);

/* ============================================
 * Rendering Functions
 * ============================================ */

/**
 * Render all passes in order
 * BufferA → BufferB → BufferC → BufferD → Image
 * 
 * @param shader Multipass shader
 * @param time Current time in seconds
 * @param mouse_x Mouse X position (0.0 to width)
 * @param mouse_y Mouse Y position (0.0 to height)
 * @param mouse_click Mouse button state
 */
void multipass_render(multipass_shader_t *shader, 
                      float time,
                      float mouse_x, float mouse_y,
                      bool mouse_click);

/**
 * Render a single pass
 * 
 * @param shader Multipass shader
 * @param pass_index Index of pass to render
 * @param time Current time
 * @param mouse_x Mouse X position
 * @param mouse_y Mouse Y position
 * @param mouse_click Mouse button state
 */
void multipass_render_pass(multipass_shader_t *shader,
                           int pass_index,
                           float time,
                           float mouse_x, float mouse_y,
                           bool mouse_click);

/**
 * Set uniforms for a pass
 * Sets iTime, iResolution, iMouse, iFrame, iChannel0-3, etc.
 * 
 * @param shader Multipass shader
 * @param pass_index Index of pass
 * @param time Current time
 * @param mouse_x Mouse X position
 * @param mouse_y Mouse Y position
 * @param mouse_click Mouse button state
 */
void multipass_set_uniforms(multipass_shader_t *shader,
                            int pass_index,
                            float time,
                            float mouse_x, float mouse_y,
                            bool mouse_click);

/**
 * Bind textures for a pass based on channel configuration
 * 
 * @param shader Multipass shader
 * @param pass_index Index of pass
 */
void multipass_bind_textures(multipass_shader_t *shader, int pass_index);

/**
 * Swap ping-pong buffers for a pass (for feedback effects)
 * 
 * @param shader Multipass shader
 * @param pass_index Index of pass
 */
void multipass_swap_buffers(multipass_shader_t *shader, int pass_index);

/**
 * Reset shader state (time, frame count, clear buffers)
 * 
 * @param shader Multipass shader
 */
void multipass_reset(multipass_shader_t *shader);

/**
 * Set resolution scale for buffer passes (performance optimization)
 * Lower values = faster but less detail
 * 
 * @param shader Multipass shader
 * @param scale Resolution scale (1.0 = full, 0.5 = half, 0.25 = quarter)
 */
void multipass_set_resolution_scale(multipass_shader_t *shader, float scale);

/**
 * Get current resolution scale
 * 
 * @param shader Multipass shader
 * @return Current resolution scale
 */
float multipass_get_resolution_scale(const multipass_shader_t *shader);

/**
 * Enable/disable adaptive resolution scaling
 * When enabled, resolution automatically adjusts to maintain target FPS
 * 
 * @param shader Multipass shader
 * @param enabled Enable adaptive scaling
 * @param target_fps Target FPS to maintain (default 60)
 * @param min_scale Minimum resolution scale (default 0.25)
 * @param max_scale Maximum resolution scale (default 1.0)
 */
void multipass_set_adaptive_resolution(multipass_shader_t *shader, 
                                        bool enabled,
                                        float target_fps,
                                        float min_scale,
                                        float max_scale);

/* Configure adaptive resolution with full options */
void multipass_configure_adaptive(multipass_shader_t *shader,
                                  const adaptive_config_t *config);

/* Set adaptive scaling mode preset */
void multipass_set_adaptive_mode(multipass_shader_t *shader, adaptive_mode_t mode);

/* Get performance statistics */
adaptive_stats_t multipass_get_adaptive_stats(const multipass_shader_t *shader);

/**
 * Check if adaptive resolution is enabled
 * 
 * @param shader Multipass shader
 * @return true if adaptive resolution is enabled
 */
bool multipass_is_adaptive_resolution(const multipass_shader_t *shader);

/**
 * Get current measured FPS
 * 
 * @param shader Multipass shader
 * @return Current FPS (smoothed average)
 */
float multipass_get_current_fps(const multipass_shader_t *shader);

/**
 * Update adaptive resolution (called internally each frame)
 * Adjusts resolution scale based on current FPS
 * 
 * @param shader Multipass shader
 * @param current_time Current time in seconds
 */
void multipass_update_adaptive_resolution(multipass_shader_t *shader, double current_time);

/* ============================================
 * Query Functions
 * ============================================ */

/**
 * Get compilation error for a pass
 * 
 * @param shader Multipass shader
 * @param pass_index Index of pass
 * @return Error message or NULL if no error
 */
const char *multipass_get_error(const multipass_shader_t *shader, int pass_index);

/**
 * Get combined error message for all passes
 * 
 * @param shader Multipass shader
 * @return Combined error message (caller must free) or NULL
 */
char *multipass_get_all_errors(const multipass_shader_t *shader);

/**
 * Check if shader has any compilation errors
 * 
 * @param shader Multipass shader
 * @return true if any pass has errors
 */
bool multipass_has_errors(const multipass_shader_t *shader);

/**
 * Check if shader is ready to render
 * 
 * @param shader Multipass shader
 * @return true if all passes are compiled and GL is initialized
 */
bool multipass_is_ready(const multipass_shader_t *shader);

/**
 * Get pass by type
 * 
 * @param shader Multipass shader
 * @param type Pass type to find
 * @return Pointer to pass or NULL if not found
 */
multipass_pass_t *multipass_get_pass_by_type(multipass_shader_t *shader, 
                                              multipass_type_t type);

/**
 * Get pass index by type
 * 
 * @param shader Multipass shader
 * @param type Pass type to find
 * @return Pass index or -1 if not found
 */
int multipass_get_pass_index(const multipass_shader_t *shader, multipass_type_t type);

/**
 * Get texture for a buffer pass (for reading in other passes)
 * Returns the "read" texture from ping-pong pair
 * 
 * @param shader Multipass shader
 * @param type Buffer type (PASS_TYPE_BUFFER_A through D)
 * @return Texture ID or 0 if not found
 */
GLuint multipass_get_buffer_texture(const multipass_shader_t *shader, 
                                     multipass_type_t type);

/* ============================================
 * Utility Functions
 * ============================================ */

/**
 * Get pass type name as string
 * 
 * @param type Pass type
 * @return Name string (static, do not free)
 */
const char *multipass_type_name(multipass_type_t type);

/**
 * Parse pass type from string
 * 
 * @param name Pass name (e.g., "Buffer A", "Image")
 * @return Pass type or PASS_TYPE_NONE if unknown
 */
multipass_type_t multipass_type_from_name(const char *name);

/**
 * Get channel source name as string
 * 
 * @param source Channel source
 * @return Name string (static, do not free)
 */
const char *multipass_channel_source_name(channel_source_t source);

/**
 * Create default channel configuration
 * 
 * @param source Channel source
 * @return Default channel configuration
 */
multipass_channel_t multipass_default_channel(channel_source_t source);

/**
 * Dump shader structure to log for debugging
 * 
 * @param shader Multipass shader
 */
void multipass_debug_dump(const multipass_shader_t *shader);

/* ============================================
 * Manifest binding API (Tier 2/3)
 * ============================================ */

/**
 * Override the channel source for a given pass channel explicitly, bypassing
 * the source-text heuristic. Marks the shader as having explicit bindings so
 * the heuristic is not consulted for any channel. Call before compilation.
 *
 * @param shader     Multipass shader
 * @param pass_type  Which pass (PASS_TYPE_IMAGE / BUFFER_A..D)
 * @param channel    Channel index 0..3
 * @param source     The channel source to bind
 */
void multipass_set_channel(multipass_shader_t *shader,
                           multipass_type_t pass_type,
                           int channel, channel_source_t source);

/**
 * Register a user uniform declared by a manifest. It is injected into the
 * shader wrapper and set every frame (from a live reactive signal, or as a
 * constant). No effect if the uniform table is full.
 *
 * @param shader Multipass shader
 * @param name   GLSL uniform name (float)
 * @param bind   Live source or UNIFORM_BIND_CONST
 * @param value  Initial / constant value
 */
void multipass_add_user_uniform(multipass_shader_t *shader,
                                const char *name, uniform_bind_t bind, float value);

/**
 * Parse a uniform-binding keyword ("cpu", "audio_bass", "const", ...).
 * Returns UNIFORM_BIND_CONST for unknown / literal values.
 */
uniform_bind_t multipass_bind_from_name(const char *name);

/**
 * Parse a channel-source keyword ("audio", "noise", "bufferA", "self", ...).
 * Returns CHANNEL_SOURCE_NONE if unknown.
 */
channel_source_t multipass_channel_source_from_name(const char *name);

#endif /* SHADER_MULTIPASS_H */