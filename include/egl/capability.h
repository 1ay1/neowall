#ifndef EGL_CAPABILITY_H
#define EGL_CAPABILITY_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* EGL version enumeration */
typedef enum {
    STATICWALL_EGL_VERSION_UNKNOWN = 0,
    STATICWALL_EGL_VERSION_1_0,
    STATICWALL_EGL_VERSION_1_1,
    STATICWALL_EGL_VERSION_1_2,
    STATICWALL_EGL_VERSION_1_3,
    STATICWALL_EGL_VERSION_1_4,
    STATICWALL_EGL_VERSION_1_5
} egl_version_t;

/* OpenGL ES version enumeration */
typedef enum {
    GLES_VERSION_NONE = 0,
    GLES_VERSION_1_0,
    GLES_VERSION_1_1,
    GLES_VERSION_2_0,
    GLES_VERSION_3_0,
    GLES_VERSION_3_1,
    GLES_VERSION_3_2
} gles_version_t;

/* EGL 1.0 capabilities */
typedef struct {
    bool available;
    bool has_initialize;
    bool has_terminate;
    bool has_get_display;
    bool has_choose_config;
    bool has_create_window_surface;
    bool has_create_pbuffer_surface;
    bool has_create_pixmap_surface;
    bool has_destroy_surface;
    bool has_query_surface;
    bool has_bind_api;
    bool has_query_api;
    bool has_wait_client;
    bool has_release_thread;
    bool has_create_pbuffer_from_client_buffer;
    bool has_surface_attrib;
    bool has_bind_tex_image;
    bool has_release_tex_image;
    bool has_swap_interval;
    bool has_create_context;
    bool has_destroy_context;
    bool has_make_current;
    bool has_get_current_context;
    bool has_get_current_surface;
    bool has_get_current_display;
    bool has_query_context;
    bool has_wait_gl;
    bool has_wait_native;
    bool has_swap_buffers;
    bool has_copy_buffers;
} egl_v10_caps_t;

/* EGL 1.1 capabilities (adds to 1.0) */
typedef struct {
    bool available;
    bool has_lock_surface;
    bool has_unlock_surface;
} egl_v11_caps_t;

/* EGL 1.2 capabilities (adds to 1.1) */
typedef struct {
    bool available;
    bool has_bind_api;
    bool has_query_api;
    bool has_wait_client;
    bool has_release_thread;
    bool has_create_pbuffer_from_client_buffer;
    bool supports_opengl_es2;
    bool supports_openvg;
} egl_v12_caps_t;

/* EGL 1.3 capabilities (adds to 1.2) */
typedef struct {
    bool available;
    bool has_surface_attrib;
    bool supports_vg_colorspace_conversion;
    bool supports_vg_alpha_format;
} egl_v13_caps_t;

/* EGL 1.4 capabilities (adds to 1.3) */
typedef struct {
    bool available;
    bool has_get_current_context;
    bool supports_multithread;
    bool supports_multiple_contexts;
    bool supports_shared_contexts;
} egl_v14_caps_t;

/* EGL 1.5 capabilities (adds to 1.4) */
typedef struct {
    bool available;
    bool has_create_sync;
    bool has_destroy_sync;
    bool has_client_wait_sync;
    bool has_get_sync_attrib;
    bool has_create_image;
    bool has_destroy_image;
    bool has_get_platform_display;
    bool has_create_platform_window_surface;
    bool has_create_platform_pixmap_surface;
    bool has_wait_sync;
    bool supports_cl_events;
    bool supports_device_query;
    bool supports_output_layers;
    bool supports_streams;
} egl_v15_caps_t;

/* OpenGL ES 1.0 capabilities */
typedef struct {
    bool available;
    bool has_fixed_function_pipeline;
    bool has_vertex_arrays;
    bool has_color_arrays;
    bool has_normal_arrays;
    bool has_texture_coord_arrays;
    bool has_matrix_stacks;
    bool has_lighting;
    bool has_fog;
    bool has_blending;
    bool has_depth_test;
    bool has_stencil_test;
    bool has_alpha_test;
    bool has_texture_2d;
    bool has_texture_env;
    int max_texture_units;
    int max_texture_size;
    int max_lights;
    int max_clip_planes;
} gles_v10_caps_t;

/* OpenGL ES 1.1 capabilities (adds to 1.0) */
typedef struct {
    bool available;
    bool has_point_sprites;
    bool has_point_size_array;
    bool has_user_clip_planes;
    bool has_vertex_buffer_objects;
    bool has_automatic_mipmap_generation;
    bool has_draw_texture;
    bool has_matrix_palette;
    bool has_byte_coordinates;
    bool has_fixed_point_extension;
    int max_palette_matrices;
    int max_vertex_units;
} gles_v11_caps_t;

/* OpenGL ES 2.0 capabilities */
typedef struct {
    bool available;
    bool has_programmable_shaders;
    bool has_vertex_shaders;
    bool has_fragment_shaders;
    bool has_glsl_100;
    bool has_framebuffer_objects;
    bool has_vertex_buffer_objects;
    bool has_texture_npot;
    bool has_depth_texture;
    bool has_float_textures;
    bool has_standard_derivatives;
    bool has_3d_textures;
    bool has_instanced_arrays;
    bool has_depth24_stencil8;
    int max_vertex_attribs;
    int max_vertex_uniform_vectors;
    int max_varying_vectors;
    int max_fragment_uniform_vectors;
    int max_texture_image_units;
    int max_vertex_texture_image_units;
    int max_combined_texture_image_units;
    int max_texture_size;
    int max_cube_map_texture_size;
    int max_renderbuffer_size;
    int max_viewport_dims[2];
} gles_v20_caps_t;

/* OpenGL ES 3.0 capabilities (adds to 2.0) */
typedef struct {
    bool available;
    bool has_glsl_300_es;
    bool has_multiple_render_targets;
    bool has_texture_3d;
    bool has_texture_arrays;
    bool has_depth_texture;
    bool has_float_textures;
    bool has_half_float_textures;
    bool has_integer_textures;
    bool has_srgb;
    bool has_vertex_array_objects;
    bool has_sampler_objects;
    bool has_sync_objects;
    bool has_transform_feedback;
    bool has_uniform_buffer_objects;
    bool has_instanced_rendering;
    bool has_occlusion_queries;
    bool has_timer_queries;
    bool has_packed_depth_stencil;
    bool has_rgb8_rgba8;
    bool has_depth_component32f;
    bool has_invalidate_framebuffer;
    bool has_blit_framebuffer;
    int max_3d_texture_size;
    int max_array_texture_layers;
    int max_color_attachments;
    int max_draw_buffers;
    int max_uniform_buffer_bindings;
    int max_uniform_block_size;
    int max_vertex_uniform_blocks;
    int max_fragment_uniform_blocks;
    int max_transform_feedback_interleaved_components;
    int max_transform_feedback_separate_attribs;
} gles_v30_caps_t;

/* OpenGL ES 3.1 capabilities (adds to 3.0) */
typedef struct {
    bool available;
    bool has_glsl_310_es;
    bool has_compute_shaders;
    bool has_shader_storage_buffer_objects;
    bool has_atomic_counters;
    bool has_shader_image_load_store;
    bool has_program_interface_query;
    bool has_indirect_draw;
    bool has_separate_shader_objects;
    bool has_texture_gather;
    bool has_stencil_texturing;
    bool has_multisample_textures;
    int max_compute_work_group_count[3];
    int max_compute_work_group_size[3];
    int max_compute_work_group_invocations;
    int max_compute_shared_memory_size;
    int max_compute_uniform_blocks;
    int max_compute_texture_image_units;
    int max_compute_atomic_counter_buffers;
    int max_compute_atomic_counters;
    int max_image_units;
    int max_combined_shader_storage_blocks;
    int max_shader_storage_block_size;
    int max_atomic_counter_buffer_bindings;
    int max_vertex_atomic_counters;
    int max_fragment_atomic_counters;
    int max_combined_atomic_counters;
} gles_v31_caps_t;

/* OpenGL ES 3.2 capabilities (adds to 3.1) */
typedef struct {
    bool available;
    bool has_glsl_320_es;
    bool has_geometry_shaders;
    bool has_tessellation_shaders;
    bool has_texture_buffer;
    bool has_texture_cube_map_array;
    bool has_sample_shading;
    bool has_multisample_interpolation;
    bool has_draw_buffers_indexed;
    bool has_primitive_bounding_box;
    bool has_debug_output;
    bool has_texture_border_clamp;
    bool has_copy_image;
    int max_geometry_input_components;
    int max_geometry_output_components;
    int max_geometry_output_vertices;
    int max_geometry_total_output_components;
    int max_geometry_uniform_blocks;
    int max_geometry_shader_invocations;
    int max_tess_control_input_components;
    int max_tess_control_output_components;
    int max_tess_control_uniform_blocks;
    int max_tess_evaluation_input_components;
    int max_tess_evaluation_output_components;
    int max_tess_evaluation_uniform_blocks;
    int max_patch_vertices;
    int max_tess_gen_level;
} gles_v32_caps_t;

/* Unified capability structure */
typedef struct {
    /* Detected versions */
    egl_version_t egl_version;
    gles_version_t gles_version;
    
    /* Version-specific capabilities */
    egl_v10_caps_t egl_v10;
    egl_v11_caps_t egl_v11;
    egl_v12_caps_t egl_v12;
    egl_v13_caps_t egl_v13;
    egl_v14_caps_t egl_v14;
    egl_v15_caps_t egl_v15;
    
    gles_v10_caps_t gles_v10;
    gles_v11_caps_t gles_v11;
    gles_v20_caps_t gles_v20;
    gles_v30_caps_t gles_v30;
    gles_v31_caps_t gles_v31;
    gles_v32_caps_t gles_v32;
    
    /* Extension support */
    bool has_egl_khr_image_base;
    bool has_egl_khr_gl_texture_2d_image;
    bool has_egl_khr_gl_texture_cubemap_image;
    bool has_egl_khr_gl_texture_3d_image;
    bool has_egl_khr_gl_renderbuffer_image;
    bool has_egl_khr_fence_sync;
    bool has_egl_khr_wait_sync;
    bool has_egl_khr_stream;
    bool has_egl_khr_platform_x11;
    bool has_egl_khr_platform_wayland;
    bool has_egl_ext_platform_base;
    
    /* OpenGL ES extensions */
    bool has_oes_texture_3d;
    bool has_oes_packed_depth_stencil;
    bool has_oes_depth_texture;
    bool has_oes_standard_derivatives;
    bool has_oes_vertex_array_object;
    bool has_oes_mapbuffer;
    bool has_oes_texture_npot;
    bool has_oes_texture_float;
    bool has_oes_texture_half_float;
    bool has_oes_element_index_uint;
    bool has_ext_texture_format_bgra8888;
    bool has_ext_color_buffer_float;
    bool has_ext_color_buffer_half_float;
    
    /* Runtime information */
    char egl_vendor[256];
    char egl_version_string[256];
    char egl_client_apis[256];
    char egl_extensions[4096];
    char gl_vendor[256];
    char gl_renderer[256];
    char gl_version[256];
    char gl_shading_language_version[256];
    char gl_extensions[8192];
} egl_capabilities_t;

/* Function prototypes */

/**
 * Detect all EGL and OpenGL ES capabilities
 * 
 * @param display EGL display
 * @param caps Output capability structure
 * @return true on success, false on failure
 */
bool egl_detect_capabilities(EGLDisplay display, egl_capabilities_t *caps);

/**
 * Detect OpenGL ES capabilities for a current context
 * 
 * @param display EGL display
 * @param context EGL context (must be current)
 * @param caps Capability structure to update
 * @return true on success, false on failure
 */
bool gles_detect_capabilities_for_context(EGLDisplay display, EGLContext context, 
                                          egl_capabilities_t *caps);

/**
 * Detect EGL version
 * 
 * @param display EGL display
 * @return EGL version enum
 */
egl_version_t egl_detect_version(EGLDisplay display);

/**
 * Detect OpenGL ES version for a context
 * 
 * @param display EGL display
 * @param context EGL context (must be current)
 * @return OpenGL ES version enum
 */
gles_version_t gles_detect_version(EGLDisplay display, EGLContext context);

/**
 * Check if EGL extension is supported
 * 
 * @param display EGL display
 * @param extension Extension name
 * @return true if supported, false otherwise
 */
bool egl_has_extension(EGLDisplay display, const char *extension);

/**
 * Check if OpenGL ES extension is supported
 * 
 * @param extension Extension name
 * @return true if supported, false otherwise
 */
bool gles_has_extension(const char *extension);

/**
 * Get human-readable version string
 * 
 * @param version EGL version enum
 * @return Version string (e.g., "1.5")
 */
const char *egl_version_string(egl_version_t version);

/**
 * Get human-readable OpenGL ES version string
 * 
 * @param version OpenGL ES version enum
 * @return Version string (e.g., "3.2")
 */
const char *gles_version_string(gles_version_t version);

/**
 * Print capability summary (for debugging)
 * 
 * @param caps Capability structure
 */
void egl_print_capabilities(const egl_capabilities_t *caps);

/**
 * Get best available OpenGL ES version
 * 
 * @param caps Capability structure
 * @return Highest available OpenGL ES version
 */
gles_version_t egl_get_best_gles_version(const egl_capabilities_t *caps);

/**
 * Check if minimum version is available
 * 
 * @param caps Capability structure
 * @param min_version Minimum required version
 * @return true if available, false otherwise
 */
bool egl_has_min_version(const egl_capabilities_t *caps, egl_version_t min_version);

/**
 * Check if minimum OpenGL ES version is available
 * 
 * @param caps Capability structure
 * @param min_version Minimum required version
 * @return true if available, false otherwise
 */
bool gles_has_min_version(const egl_capabilities_t *caps, gles_version_t min_version);

#endif /* EGL_CAPABILITY_H */