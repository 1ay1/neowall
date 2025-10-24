#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#ifdef HAVE_GLES1
#include <GLES/gl.h>
#endif
#include <GLES2/gl2.h>
#ifdef HAVE_GLES3
#include <GLES3/gl3.h>
#endif
#include "../../include/egl/capability.h"

/* Helper: Check if string contains substring */
static bool has_substring(const char *haystack, const char *needle) {
    if (!haystack || !needle) {
        return false;
    }
    return strstr(haystack, needle) != NULL;
}

/* Helper: Parse version string (format: "major.minor") */
static void parse_version(const char *version_str, int *major, int *minor) __attribute__((unused));
static void parse_version(const char *version_str, int *major, int *minor) {
    *major = 0;
    *minor = 0;
    
    if (version_str) {
        sscanf(version_str, "%d.%d", major, minor);
    }
}
#define UNUSED(x) (void)(x)

/* Detect EGL version */
egl_version_t egl_detect_version(EGLDisplay display) {
    if (display == EGL_NO_DISPLAY) {
        return STATICWALL_EGL_VERSION_UNKNOWN;
    }
    
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        return STATICWALL_EGL_VERSION_UNKNOWN;
    }
    
    if (major == 1) {
        if (minor >= 5) return STATICWALL_EGL_VERSION_1_5;
        if (minor >= 4) return STATICWALL_EGL_VERSION_1_4;
        if (minor >= 3) return STATICWALL_EGL_VERSION_1_3;
        if (minor >= 2) return STATICWALL_EGL_VERSION_1_2;
        if (minor >= 1) return STATICWALL_EGL_VERSION_1_1;
        return STATICWALL_EGL_VERSION_1_0;
    }
    
    return STATICWALL_EGL_VERSION_UNKNOWN;
}

/* Detect OpenGL ES version */
gles_version_t gles_detect_version(EGLDisplay display, EGLContext context) {
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        return GLES_VERSION_NONE;
    }
    
    const char *version = (const char*)glGetString(GL_VERSION);
    if (!version) {
        return GLES_VERSION_NONE;
    }
    
    int major = 0, minor = 0;
    
    /* Parse "OpenGL ES X.Y" or "OpenGL ES-CM X.Y" */
    if (sscanf(version, "OpenGL ES %d.%d", &major, &minor) == 2 ||
        sscanf(version, "OpenGL ES-CM %d.%d", &major, &minor) == 2) {
        
        if (major == 1) {
            if (minor >= 1) return GLES_VERSION_1_1;
            return GLES_VERSION_1_0;
        } else if (major == 2) {
            return GLES_VERSION_2_0;
        } else if (major == 3) {
            if (minor >= 2) return GLES_VERSION_3_2;
            if (minor >= 1) return GLES_VERSION_3_1;
            return GLES_VERSION_3_0;
        }
    }
    
    return GLES_VERSION_NONE;
}

/* Check if EGL extension is supported */
bool egl_has_extension(EGLDisplay display, const char *extension) {
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions || !extension) {
        return false;
    }
    return has_substring(extensions, extension);
}

/* Check if OpenGL ES extension is supported */
bool gles_has_extension(const char *extension) {
    const char *extensions = (const char*)glGetString(GL_EXTENSIONS);
    if (!extensions || !extension) {
        return false;
    }
    return has_substring(extensions, extension);
}

/* Detect EGL 1.0 capabilities */
static void detect_egl_v10_caps(EGLDisplay display, egl_v10_caps_t *caps) {
    UNUSED(display);
    memset(caps, 0, sizeof(*caps));
    
    /* All functions are mandatory in EGL 1.0 */
    caps->available = true;
    caps->has_initialize = true;
    caps->has_terminate = true;
    caps->has_get_display = true;
    caps->has_choose_config = true;
    caps->has_create_window_surface = true;
    caps->has_create_pbuffer_surface = true;
    caps->has_create_pixmap_surface = true;
    caps->has_destroy_surface = true;
    caps->has_query_surface = true;
    caps->has_swap_interval = true;
    caps->has_create_context = true;
    caps->has_destroy_context = true;
    caps->has_make_current = true;
    caps->has_get_current_context = true;
    caps->has_get_current_surface = true;
    caps->has_get_current_display = true;
    caps->has_query_context = true;
    caps->has_wait_gl = true;
    caps->has_wait_native = true;
    caps->has_swap_buffers = true;
    caps->has_copy_buffers = true;
}

/* Detect EGL 1.1 capabilities */
static void detect_egl_v11_caps(EGLDisplay display, egl_v11_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
    caps->available = true;
    caps->has_lock_surface = egl_has_extension(display, "EGL_KHR_lock_surface");
    caps->has_unlock_surface = caps->has_lock_surface;
}

/* Detect EGL 1.2 capabilities */
static void detect_egl_v12_caps(EGLDisplay display, egl_v12_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
    caps->available = true;
    caps->has_bind_api = true;
    caps->has_query_api = true;
    caps->has_wait_client = true;
    caps->has_release_thread = true;
    caps->has_create_pbuffer_from_client_buffer = true;
    caps->supports_opengl_es2 = has_substring(eglQueryString(display, EGL_CLIENT_APIS), "OpenGL_ES");
    caps->supports_openvg = has_substring(eglQueryString(display, EGL_CLIENT_APIS), "OpenVG");
}

/* Detect EGL 1.3 capabilities */
static void detect_egl_v13_caps(EGLDisplay display, egl_v13_caps_t *caps) {
    UNUSED(display);
    memset(caps, 0, sizeof(*caps));
    caps->available = true;
    caps->has_surface_attrib = true;
    caps->supports_vg_colorspace_conversion = true;
    caps->supports_vg_alpha_format = true;
}

/* Detect EGL 1.4 capabilities */
static void detect_egl_v14_caps(EGLDisplay display, egl_v14_caps_t *caps) {
    UNUSED(display);
    memset(caps, 0, sizeof(*caps));
    caps->available = true;
    caps->has_get_current_context = true;
    caps->supports_multithread = true;
    caps->supports_multiple_contexts = true;
    caps->supports_shared_contexts = true;
}

/* Detect EGL 1.5 capabilities */
static void detect_egl_v15_caps(EGLDisplay display, egl_v15_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
    caps->available = true;
    caps->has_create_sync = egl_has_extension(display, "EGL_KHR_fence_sync");
    caps->has_destroy_sync = caps->has_create_sync;
    caps->has_client_wait_sync = caps->has_create_sync;
    caps->has_get_sync_attrib = caps->has_create_sync;
    caps->has_create_image = egl_has_extension(display, "EGL_KHR_image_base");
    caps->has_destroy_image = caps->has_create_image;
    caps->has_get_platform_display = egl_has_extension(display, "EGL_EXT_platform_base");
    caps->has_create_platform_window_surface = caps->has_get_platform_display;
    caps->has_create_platform_pixmap_surface = caps->has_get_platform_display;
    caps->has_wait_sync = egl_has_extension(display, "EGL_KHR_wait_sync");
    caps->supports_cl_events = egl_has_extension(display, "EGL_KHR_cl_event2");
    caps->supports_device_query = egl_has_extension(display, "EGL_EXT_device_query");
    caps->supports_output_layers = egl_has_extension(display, "EGL_EXT_output_base");
    caps->supports_streams = egl_has_extension(display, "EGL_KHR_stream");
}

/* Detect OpenGL ES 1.0 capabilities */
static void detect_gles_v10_caps(gles_v10_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
#ifdef HAVE_GLES1
    caps->available = true;
    caps->has_fixed_function_pipeline = true;
    caps->has_vertex_arrays = true;
    caps->has_color_arrays = true;
    caps->has_normal_arrays = true;
    caps->has_texture_coord_arrays = true;
    caps->has_matrix_stacks = true;
    caps->has_lighting = true;
    caps->has_fog = true;
    caps->has_blending = true;
    caps->has_depth_test = true;
    caps->has_stencil_test = true;
    caps->has_alpha_test = true;
    caps->has_texture_2d = true;
    caps->has_texture_env = true;
    
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &caps->max_texture_units);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps->max_texture_size);
    glGetIntegerv(GL_MAX_LIGHTS, &caps->max_lights);
    glGetIntegerv(GL_MAX_CLIP_PLANES, &caps->max_clip_planes);
#else
    caps->available = false;
#endif
}

/* Detect OpenGL ES 1.1 capabilities */
static void detect_gles_v11_caps(gles_v11_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
#ifdef HAVE_GLES1
    caps->available = true;
    caps->has_point_sprites = gles_has_extension("GL_OES_point_sprite");
    caps->has_point_size_array = gles_has_extension("GL_OES_point_size_array");
    caps->has_user_clip_planes = true;
    caps->has_vertex_buffer_objects = gles_has_extension("GL_OES_vertex_buffer_object");
    caps->has_automatic_mipmap_generation = true;
    caps->has_draw_texture = gles_has_extension("GL_OES_draw_texture");
    caps->has_matrix_palette = gles_has_extension("GL_OES_matrix_palette");
    caps->has_byte_coordinates = gles_has_extension("GL_OES_byte_coordinates");
    caps->has_fixed_point_extension = gles_has_extension("GL_OES_fixed_point");
#else
    caps->available = false;
#endif
}

/* Detect OpenGL ES 2.0 capabilities */
static void detect_gles_v20_caps(gles_v20_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
    caps->available = true;
    caps->has_programmable_shaders = true;
    caps->has_vertex_shaders = true;
    caps->has_fragment_shaders = true;
    caps->has_glsl_100 = true;
    caps->has_framebuffer_objects = true;
    caps->has_vertex_buffer_objects = true;
    caps->has_texture_npot = gles_has_extension("GL_OES_texture_npot");
    caps->has_depth_texture = gles_has_extension("GL_OES_depth_texture");
    caps->has_float_textures = gles_has_extension("GL_OES_texture_float");
    caps->has_standard_derivatives = gles_has_extension("GL_OES_standard_derivatives");
    caps->has_3d_textures = gles_has_extension("GL_OES_texture_3D");
    caps->has_instanced_arrays = gles_has_extension("GL_EXT_instanced_arrays");
    caps->has_depth24_stencil8 = gles_has_extension("GL_OES_packed_depth_stencil");
    
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &caps->max_vertex_attribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &caps->max_vertex_uniform_vectors);
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &caps->max_varying_vectors);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &caps->max_fragment_uniform_vectors);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &caps->max_texture_image_units);
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &caps->max_vertex_texture_image_units);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &caps->max_combined_texture_image_units);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps->max_texture_size);
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &caps->max_cube_map_texture_size);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &caps->max_renderbuffer_size);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, caps->max_viewport_dims);
}

/* Detect OpenGL ES 3.0 capabilities */
static void detect_gles_v30_caps(gles_v30_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
#ifdef HAVE_GLES3
    caps->available = true;
    caps->has_glsl_300_es = true;
    caps->has_multiple_render_targets = true;
    caps->has_texture_3d = true;
    caps->has_texture_arrays = true;
    caps->has_depth_texture = true;
    caps->has_float_textures = true;
    caps->has_half_float_textures = true;
    caps->has_integer_textures = true;
    caps->has_srgb = true;
    caps->has_vertex_array_objects = true;
    caps->has_sampler_objects = true;
    caps->has_sync_objects = true;
    caps->has_transform_feedback = true;
    caps->has_uniform_buffer_objects = true;
    caps->has_instanced_rendering = true;
    caps->has_occlusion_queries = true;
    caps->has_timer_queries = gles_has_extension("GL_EXT_disjoint_timer_query");
    caps->has_packed_depth_stencil = true;
    caps->has_rgb8_rgba8 = true;
    caps->has_depth_component32f = true;
    caps->has_invalidate_framebuffer = true;
    caps->has_blit_framebuffer = true;
    
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &caps->max_3d_texture_size);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &caps->max_array_texture_layers);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &caps->max_color_attachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &caps->max_draw_buffers);
#else
    caps->available = false;
#endif
}

/* Detect OpenGL ES 3.1 capabilities */
static void detect_gles_v31_caps(gles_v31_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
#ifdef HAVE_GLES31
    caps->available = true;
    caps->has_glsl_310_es = true;
    caps->has_compute_shaders = true;
    caps->has_shader_storage_buffer_objects = true;
    caps->has_atomic_counters = true;
    caps->has_shader_image_load_store = true;
    caps->has_program_interface_query = true;
    caps->has_indirect_draw = true;
    caps->has_separate_shader_objects = true;
    caps->has_texture_gather = true;
    caps->has_stencil_texturing = true;
    caps->has_multisample_textures = true;
#else
    caps->available = false;
#endif
}

/* Detect OpenGL ES 3.2 capabilities */
static void detect_gles_v32_caps(gles_v32_caps_t *caps) {
    memset(caps, 0, sizeof(*caps));
#ifdef HAVE_GLES32
    caps->available = true;
    caps->has_glsl_320_es = true;
    caps->has_geometry_shaders = true;
    caps->has_tessellation_shaders = true;
    caps->has_texture_buffer = true;
    caps->has_texture_cube_map_array = true;
    caps->has_sample_shading = true;
    caps->has_multisample_interpolation = true;
    caps->has_draw_buffers_indexed = true;
    caps->has_primitive_bounding_box = true;
    caps->has_debug_output = gles_has_extension("GL_KHR_debug");
    caps->has_texture_border_clamp = true;
    caps->has_copy_image = true;
#else
    caps->available = false;
#endif
}

/* Main capability detection function */
bool egl_detect_capabilities(EGLDisplay display, egl_capabilities_t *caps) {
    if (!caps || display == EGL_NO_DISPLAY) {
        return false;
    }
    
    memset(caps, 0, sizeof(*caps));
    
    /* Detect EGL version */
    caps->egl_version = egl_detect_version(display);
    
    /* Get EGL strings */
    const char *vendor = eglQueryString(display, EGL_VENDOR);
    const char *version = eglQueryString(display, EGL_VERSION);
    const char *client_apis = eglQueryString(display, EGL_CLIENT_APIS);
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    
    if (vendor) strncpy(caps->egl_vendor, vendor, sizeof(caps->egl_vendor) - 1);
    if (version) strncpy(caps->egl_version_string, version, sizeof(caps->egl_version_string) - 1);
    if (client_apis) strncpy(caps->egl_client_apis, client_apis, sizeof(caps->egl_client_apis) - 1);
    if (extensions) strncpy(caps->egl_extensions, extensions, sizeof(caps->egl_extensions) - 1);
    
    /* Detect EGL capabilities by version */
    if (caps->egl_version >= STATICWALL_EGL_VERSION_1_0) {
        detect_egl_v10_caps(display, &caps->egl_v10);
    }
    if (caps->egl_version >= STATICWALL_EGL_VERSION_1_1) {
        detect_egl_v11_caps(display, &caps->egl_v11);
    }
    if (caps->egl_version >= STATICWALL_EGL_VERSION_1_2) {
        detect_egl_v12_caps(display, &caps->egl_v12);
    }
    if (caps->egl_version >= STATICWALL_EGL_VERSION_1_3) {
        detect_egl_v13_caps(display, &caps->egl_v13);
    }
    if (caps->egl_version >= STATICWALL_EGL_VERSION_1_4) {
        detect_egl_v14_caps(display, &caps->egl_v14);
    }
    if (caps->egl_version >= STATICWALL_EGL_VERSION_1_5) {
        detect_egl_v15_caps(display, &caps->egl_v15);
    }
    
    /* Detect EGL extensions - Table-driven approach */
    static const struct {
        const char *name;
        size_t offset;
    } egl_extension_table[] = {
        {"EGL_KHR_image_base", offsetof(egl_capabilities_t, has_egl_khr_image_base)},
        {"EGL_KHR_gl_texture_2d_image", offsetof(egl_capabilities_t, has_egl_khr_gl_texture_2d_image)},
        {"EGL_KHR_gl_texture_cubemap_image", offsetof(egl_capabilities_t, has_egl_khr_gl_texture_cubemap_image)},
        {"EGL_KHR_gl_texture_3D_image", offsetof(egl_capabilities_t, has_egl_khr_gl_texture_3d_image)},
        {"EGL_KHR_gl_renderbuffer_image", offsetof(egl_capabilities_t, has_egl_khr_gl_renderbuffer_image)},
        {"EGL_KHR_fence_sync", offsetof(egl_capabilities_t, has_egl_khr_fence_sync)},
        {"EGL_KHR_wait_sync", offsetof(egl_capabilities_t, has_egl_khr_wait_sync)},
        {"EGL_KHR_stream", offsetof(egl_capabilities_t, has_egl_khr_stream)},
        {"EGL_KHR_platform_x11", offsetof(egl_capabilities_t, has_egl_khr_platform_x11)},
        {"EGL_KHR_platform_wayland", offsetof(egl_capabilities_t, has_egl_khr_platform_wayland)},
        {"EGL_EXT_platform_base", offsetof(egl_capabilities_t, has_egl_ext_platform_base)},
    };
    
    for (size_t i = 0; i < sizeof(egl_extension_table) / sizeof(egl_extension_table[0]); i++) {
        bool *flag = (bool*)((char*)caps + egl_extension_table[i].offset);
        *flag = egl_has_extension(display, egl_extension_table[i].name);
    }
    
    /* Note: OpenGL ES capabilities must be detected after a context is current */
    caps->gles_version = GLES_VERSION_NONE;
    
    return true;
}

/* Detect OpenGL ES capabilities (call after making context current) */
bool gles_detect_capabilities_for_context(EGLDisplay display, EGLContext context, egl_capabilities_t *caps) {
    if (!caps || display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        return false;
    }
    
    /* Detect OpenGL ES version */
    caps->gles_version = gles_detect_version(display, context);
    
    /* Get GL strings */
    const char *gl_vendor = (const char*)glGetString(GL_VENDOR);
    const char *gl_renderer = (const char*)glGetString(GL_RENDERER);
    const char *gl_version = (const char*)glGetString(GL_VERSION);
    const char *gl_shading = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    const char *gl_extensions = (const char*)glGetString(GL_EXTENSIONS);
    
    if (gl_vendor) strncpy(caps->gl_vendor, gl_vendor, sizeof(caps->gl_vendor) - 1);
    if (gl_renderer) strncpy(caps->gl_renderer, gl_renderer, sizeof(caps->gl_renderer) - 1);
    if (gl_version) strncpy(caps->gl_version, gl_version, sizeof(caps->gl_version) - 1);
    if (gl_shading) strncpy(caps->gl_shading_language_version, gl_shading, sizeof(caps->gl_shading_language_version) - 1);
    if (gl_extensions) strncpy(caps->gl_extensions, gl_extensions, sizeof(caps->gl_extensions) - 1);
    
    /* Detect capabilities by version */
    if (caps->gles_version >= GLES_VERSION_1_0) {
        detect_gles_v10_caps(&caps->gles_v10);
    }
    if (caps->gles_version >= GLES_VERSION_1_1) {
        detect_gles_v11_caps(&caps->gles_v11);
    }
    if (caps->gles_version >= GLES_VERSION_2_0) {
        detect_gles_v20_caps(&caps->gles_v20);
    }
    if (caps->gles_version >= GLES_VERSION_3_0) {
        detect_gles_v30_caps(&caps->gles_v30);
    }
    if (caps->gles_version >= GLES_VERSION_3_1) {
        detect_gles_v31_caps(&caps->gles_v31);
    }
    if (caps->gles_version >= GLES_VERSION_3_2) {
        detect_gles_v32_caps(&caps->gles_v32);
    }
    
    /* Detect OpenGL ES extensions - Table-driven approach */
    static const struct {
        const char *name;
        size_t offset;
    } gles_extension_table[] = {
        {"GL_OES_texture_3D", offsetof(egl_capabilities_t, has_oes_texture_3d)},
        {"GL_OES_packed_depth_stencil", offsetof(egl_capabilities_t, has_oes_packed_depth_stencil)},
        {"GL_OES_depth_texture", offsetof(egl_capabilities_t, has_oes_depth_texture)},
        {"GL_OES_standard_derivatives", offsetof(egl_capabilities_t, has_oes_standard_derivatives)},
        {"GL_OES_vertex_array_object", offsetof(egl_capabilities_t, has_oes_vertex_array_object)},
        {"GL_OES_mapbuffer", offsetof(egl_capabilities_t, has_oes_mapbuffer)},
        {"GL_OES_texture_npot", offsetof(egl_capabilities_t, has_oes_texture_npot)},
        {"GL_OES_texture_float", offsetof(egl_capabilities_t, has_oes_texture_float)},
        {"GL_OES_texture_half_float", offsetof(egl_capabilities_t, has_oes_texture_half_float)},
        {"GL_OES_element_index_uint", offsetof(egl_capabilities_t, has_oes_element_index_uint)},
        {"GL_EXT_texture_format_BGRA8888", offsetof(egl_capabilities_t, has_ext_texture_format_bgra8888)},
        {"GL_EXT_color_buffer_float", offsetof(egl_capabilities_t, has_ext_color_buffer_float)},
        {"GL_EXT_color_buffer_half_float", offsetof(egl_capabilities_t, has_ext_color_buffer_half_float)},
    };
    
    for (size_t i = 0; i < sizeof(gles_extension_table) / sizeof(gles_extension_table[0]); i++) {
        bool *flag = (bool*)((char*)caps + gles_extension_table[i].offset);
        *flag = gles_has_extension(gles_extension_table[i].name);
    }
    
    return true;
}

/* Get version strings - Table lookup */
const char *egl_version_string(egl_version_t version) {
    static const char *version_strings[] = {
        [STATICWALL_EGL_VERSION_UNKNOWN] = "Unknown",
        [STATICWALL_EGL_VERSION_1_0] = "1.0",
        [STATICWALL_EGL_VERSION_1_1] = "1.1",
        [STATICWALL_EGL_VERSION_1_2] = "1.2",
        [STATICWALL_EGL_VERSION_1_3] = "1.3",
        [STATICWALL_EGL_VERSION_1_4] = "1.4",
        [STATICWALL_EGL_VERSION_1_5] = "1.5",
    };
    
    if (version >= 0 && version < (int)(sizeof(version_strings) / sizeof(version_strings[0]))) {
        return version_strings[version] ? version_strings[version] : "Unknown";
    }
    return "Unknown";
}

const char *gles_version_string(gles_version_t version) {
    switch (version) {
        case GLES_VERSION_NONE: return "None";
        case GLES_VERSION_1_0: return "1.0";
        case GLES_VERSION_1_1: return "1.1";
        case GLES_VERSION_2_0: return "2.0";
        case GLES_VERSION_3_0: return "3.0";
        case GLES_VERSION_3_1: return "3.1";
        case GLES_VERSION_3_2: return "3.2";
        default: return "None";
    }
}

/* Get best available version */
gles_version_t egl_get_best_gles_version(const egl_capabilities_t *caps) {
    if (!caps) return GLES_VERSION_NONE;
    if (caps->gles_v32.available) return GLES_VERSION_3_2;
    if (caps->gles_v31.available) return GLES_VERSION_3_1;
    if (caps->gles_v30.available) return GLES_VERSION_3_0;
    if (caps->gles_v20.available) return GLES_VERSION_2_0;
    if (caps->gles_v11.available) return GLES_VERSION_1_1;
    if (caps->gles_v10.available) return GLES_VERSION_1_0;
    return GLES_VERSION_NONE;
}

/* Check minimum version */
bool egl_has_min_version(const egl_capabilities_t *caps, egl_version_t min_version) {
    return caps && (caps->egl_version >= min_version);
}

bool gles_has_min_version(const egl_capabilities_t *caps, gles_version_t min_version) {
    return caps && (caps->gles_version >= min_version);
}

/* Print capability summary */
void egl_print_capabilities(const egl_capabilities_t *caps) {
    if (!caps) return;
    
    printf("=== EGL/OpenGL ES Capabilities ===\n");
    printf("EGL Version: %s (%s)\n", egl_version_string(caps->egl_version), caps->egl_version_string);
    printf("EGL Vendor: %s\n", caps->egl_vendor);
    printf("EGL Client APIs: %s\n", caps->egl_client_apis);
    
    printf("\nOpenGL ES Version: %s (%s)\n", gles_version_string(caps->gles_version), caps->gl_version);
    printf("GL Vendor: %s\n", caps->gl_vendor);
    printf("GL Renderer: %s\n", caps->gl_renderer);
    printf("GL Shading Language: %s\n", caps->gl_shading_language_version);
    
    if (caps->gles_version >= GLES_VERSION_2_0) {
        printf("\nOpenGL ES 2.0 Capabilities:\n");
        printf("  Max vertex attributes: %d\n", caps->gles_v20.max_vertex_attribs);
        printf("  Max texture units: %d\n", caps->gles_v20.max_texture_image_units);
        printf("  Max texture size: %d\n", caps->gles_v20.max_texture_size);
        printf("  NPOT textures: %s\n", caps->gles_v20.has_texture_npot ? "Yes" : "No");
        printf("  Float textures: %s\n", caps->gles_v20.has_float_textures ? "Yes" : "No");
        printf("  Standard derivatives: %s\n", caps->gles_v20.has_standard_derivatives ? "Yes" : "No");
    }
    
    if (caps->gles_version >= GLES_VERSION_3_0) {
        printf("\nOpenGL ES 3.0 Features:\n");
        printf("  Multiple render targets: Yes\n");
        printf("  Transform feedback: Yes\n");
        printf("  Uniform buffer objects: Yes\n");
        printf("  Instanced rendering: Yes\n");
        printf("  Max 3D texture size: %d\n", caps->gles_v30.max_3d_texture_size);
        printf("  Max color attachments: %d\n", caps->gles_v30.max_color_attachments);
    }
    
    if (caps->gles_version >= GLES_VERSION_3_1) {
        printf("\nOpenGL ES 3.1 Features:\n");
        printf("  Compute shaders: Yes\n");
        printf("  Shader storage buffers: Yes\n");
        printf("  Atomic counters: Yes\n");
    }
    
    if (caps->gles_version >= GLES_VERSION_3_2) {
        printf("\nOpenGL ES 3.2 Features:\n");
        printf("  Geometry shaders: Yes\n");
        printf("  Tessellation shaders: Yes\n");
        printf("  Texture buffers: Yes\n");
    }
    
    printf("\n=================================\n");
}