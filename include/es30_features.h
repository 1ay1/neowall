/**
 * OpenGL ES 3.0 Advanced Features
 * 
 * This module provides advanced ES 3.0 features including:
 * - Multiple Render Targets (MRT)
 * - Uniform Buffer Objects (UBO)
 * - 3D Textures
 * - Texture Arrays
 * - Enhanced texture formats
 */

#ifndef ES30_FEATURES_H
#define ES30_FEATURES_H

#include <GLES3/gl3.h>
#include <stdbool.h>

/* Maximum number of render targets for MRT */
#define MAX_MRT_TARGETS 4

/* Maximum UBO size (in bytes) - ES 3.0 minimum is 16KB */
#define MAX_UBO_SIZE 16384

/* ============================================================================
 * Multiple Render Targets (MRT)
 * ============================================================================ */

/**
 * Multiple Render Target framebuffer
 * Allows rendering to multiple textures simultaneously
 */
typedef struct {
    GLuint fbo;                           /* Framebuffer object */
    GLuint textures[MAX_MRT_TARGETS];     /* Output textures */
    int num_targets;                      /* Number of active targets */
    int width;                            /* Framebuffer width */
    int height;                           /* Framebuffer height */
    GLenum format;                        /* Texture format */
} mrt_framebuffer_t;

/**
 * Create an MRT framebuffer with multiple color attachments
 * 
 * @param width Framebuffer width in pixels
 * @param height Framebuffer height in pixels
 * @param num_targets Number of render targets (1-4)
 * @param format Texture internal format (e.g., GL_RGBA8, GL_RGBA16F)
 * @return Pointer to MRT framebuffer, or NULL on failure
 */
mrt_framebuffer_t *mrt_create(int width, int height, int num_targets, GLenum format);

/**
 * Bind MRT framebuffer for rendering
 * 
 * @param mrt MRT framebuffer to bind (NULL to unbind)
 */
void mrt_bind(mrt_framebuffer_t *mrt);

/**
 * Bind MRT textures as samplers for reading
 * 
 * @param mrt MRT framebuffer
 * @param start_unit First texture unit to bind to (e.g., 0 for GL_TEXTURE0)
 */
void mrt_bind_textures(mrt_framebuffer_t *mrt, int start_unit);

/**
 * Resize MRT framebuffer
 * 
 * @param mrt MRT framebuffer
 * @param width New width
 * @param height New height
 * @return true on success, false on failure
 */
bool mrt_resize(mrt_framebuffer_t *mrt, int width, int height);

/**
 * Destroy MRT framebuffer and free resources
 * 
 * @param mrt MRT framebuffer to destroy
 */
void mrt_destroy(mrt_framebuffer_t *mrt);

/* ============================================================================
 * Uniform Buffer Objects (UBO)
 * ============================================================================ */

/**
 * Standard Shadertoy uniform block (std140 layout)
 * Total size: 96 bytes (aligned to 16-byte boundaries)
 */
typedef struct {
    float iResolution[4];      /* vec3 + padding: (width, height, aspect, pad) */
    float iTime;               /* Shader playback time in seconds */
    float iTimeDelta;          /* Time since last frame */
    float iFrameRate;          /* Frames per second */
    int iFrame;                /* Current frame number */
    float iMouse[4];           /* vec4: (x, y, click_x, click_y) */
    float iDate[4];            /* vec4: (year, month, day, seconds) */
    float iSampleRate;         /* Audio sample rate */
    float _padding[3];         /* Align to 16 bytes */
} shadertoy_uniforms_t;

/**
 * Custom uniform block descriptor
 */
typedef struct {
    GLuint ubo;                /* UBO handle */
    GLuint binding_point;      /* Binding point index */
    size_t size;               /* Buffer size in bytes */
    void *data;                /* CPU-side data copy */
} ubo_t;

/**
 * Create a Uniform Buffer Object
 * 
 * @param size Size of the buffer in bytes
 * @param data Initial data (can be NULL)
 * @param usage Usage hint (GL_STATIC_DRAW, GL_DYNAMIC_DRAW, etc.)
 * @return Pointer to UBO structure, or NULL on failure
 */
ubo_t *ubo_create(size_t size, const void *data, GLenum usage);

/**
 * Create standard Shadertoy uniforms UBO
 * 
 * @return Pointer to UBO structure, or NULL on failure
 */
ubo_t *ubo_create_shadertoy_uniforms(void);

/**
 * Update UBO data
 * 
 * @param ubo UBO to update
 * @param offset Offset into buffer in bytes
 * @param size Size of data to update
 * @param data Data to write
 */
void ubo_update(ubo_t *ubo, size_t offset, size_t size, const void *data);

/**
 * Update full Shadertoy uniforms UBO
 * 
 * @param ubo UBO to update
 * @param uniforms Uniform data
 */
void ubo_update_shadertoy_uniforms(ubo_t *ubo, const shadertoy_uniforms_t *uniforms);

/**
 * Bind UBO to a specific binding point
 * 
 * @param ubo UBO to bind
 * @param binding_point Binding point index
 */
void ubo_bind_base(ubo_t *ubo, GLuint binding_point);

/**
 * Bind UBO to a shader program's uniform block
 * 
 * @param ubo UBO to bind
 * @param program Shader program
 * @param block_name Name of the uniform block in the shader
 * @return true on success, false on failure
 */
bool ubo_bind_to_program(ubo_t *ubo, GLuint program, const char *block_name);

/**
 * Destroy UBO and free resources
 * 
 * @param ubo UBO to destroy
 */
void ubo_destroy(ubo_t *ubo);

/* ============================================================================
 * 3D Textures
 * ============================================================================ */

/**
 * 3D texture descriptor
 */
typedef struct {
    GLuint texture;            /* Texture handle */
    int width;                 /* Width in pixels */
    int height;                /* Height in pixels */
    int depth;                 /* Depth in layers */
    GLenum format;             /* Internal format */
} texture3d_t;

/**
 * Create a 3D texture
 * 
 * @param width Texture width
 * @param height Texture height
 * @param depth Texture depth (number of layers)
 * @param internal_format Internal format (e.g., GL_RGBA8, GL_R16F)
 * @param format Pixel data format (e.g., GL_RGBA, GL_RED)
 * @param type Pixel data type (e.g., GL_UNSIGNED_BYTE, GL_FLOAT)
 * @param data Initial pixel data (can be NULL)
 * @return Pointer to 3D texture structure, or NULL on failure
 */
texture3d_t *texture3d_create(int width, int height, int depth,
                              GLenum internal_format,
                              GLenum format, GLenum type,
                              const void *data);

/**
 * Update 3D texture data
 * 
 * @param tex 3D texture to update
 * @param level Mipmap level
 * @param xoffset X offset in pixels
 * @param yoffset Y offset in pixels
 * @param zoffset Z offset in layers
 * @param width Width of region to update
 * @param height Height of region to update
 * @param depth Depth of region to update
 * @param format Pixel data format
 * @param type Pixel data type
 * @param data Pixel data
 */
void texture3d_update(texture3d_t *tex, int level,
                      int xoffset, int yoffset, int zoffset,
                      int width, int height, int depth,
                      GLenum format, GLenum type,
                      const void *data);

/**
 * Bind 3D texture to a texture unit
 * 
 * @param tex 3D texture to bind
 * @param unit Texture unit (0 for GL_TEXTURE0, etc.)
 */
void texture3d_bind(texture3d_t *tex, int unit);

/**
 * Generate procedural 3D noise texture
 * 
 * @param width Texture width
 * @param height Texture height
 * @param depth Texture depth
 * @param octaves Number of noise octaves
 * @param seed Random seed
 * @return Pointer to 3D texture structure, or NULL on failure
 */
texture3d_t *texture3d_create_noise(int width, int height, int depth, 
                                    int octaves, unsigned int seed);

/**
 * Destroy 3D texture and free resources
 * 
 * @param tex 3D texture to destroy
 */
void texture3d_destroy(texture3d_t *tex);

/* ============================================================================
 * Capability Detection
 * ============================================================================ */

/**
 * ES 3.0 feature support flags
 */
typedef struct {
    bool mrt;                  /* Multiple Render Targets */
    bool ubo;                  /* Uniform Buffer Objects */
    bool texture_3d;           /* 3D textures */
    bool texture_float;        /* Floating point textures */
    bool texture_half_float;   /* Half float textures */
    bool texture_integer;      /* Integer textures */
    bool instancing;           /* Instanced rendering */
    bool transform_feedback;   /* Transform feedback */
    bool srgb;                 /* sRGB textures and framebuffers */
    bool npot;                 /* Non-power-of-two textures */
    int max_color_attachments; /* Max MRT targets */
    int max_ubo_size;          /* Max UBO size in bytes */
    int max_3d_texture_size;   /* Max 3D texture dimension */
} es30_capabilities_t;

/**
 * Query ES 3.0 capabilities
 * 
 * @param caps Pointer to capabilities structure to fill
 * @return true if ES 3.0 is supported, false otherwise
 */
bool es30_query_capabilities(es30_capabilities_t *caps);

/**
 * Check if ES 3.0 is available
 * 
 * @return true if ES 3.0 or higher is supported
 */
bool es30_is_available(void);

/**
 * Get maximum number of color attachments for MRT
 * 
 * @return Maximum number of simultaneous render targets
 */
int es30_get_max_color_attachments(void);

/**
 * Get maximum UBO size
 * 
 * @return Maximum uniform buffer size in bytes
 */
int es30_get_max_ubo_size(void);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get human-readable name for texture format
 * 
 * @param format GL texture format enum
 * @return Format name string
 */
const char *es30_get_format_name(GLenum format);

/**
 * Get bytes per pixel for a given format
 * 
 * @param format GL texture format
 * @param type GL data type
 * @return Bytes per pixel
 */
int es30_get_format_size(GLenum format, GLenum type);

/**
 * Check if format is supported for MRT
 * 
 * @param format GL texture format
 * @return true if format can be used for MRT
 */
bool es30_is_renderable_format(GLenum format);

#endif /* ES30_FEATURES_H */