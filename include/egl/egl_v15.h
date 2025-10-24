#ifndef EGL_V15_H
#define EGL_V15_H

#include <stdbool.h>
#include <stdint.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/**
 * EGL 1.5 Implementation Header
 * 
 * EGL 1.5 (2014) introduces:
 * - Platform-specific display creation (eglGetPlatformDisplay)
 * - Sync objects for explicit GPU/CPU synchronization
 * - Better error reporting
 * - Native rendering support
 * - Improved Wayland integration
 * - OpenGL ES 3.x optimizations
 */

/* Platform types for eglGetPlatformDisplay */
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif

/* Sync object types */
#ifndef EGL_SYNC_FENCE
#define EGL_SYNC_FENCE 0x30F9
#endif

#ifndef EGL_NO_SYNC
#define EGL_NO_SYNC ((EGLSync)0)
#endif

/* EGLSync type for older headers */
#ifndef EGL_VERSION_1_5
typedef void *EGLSync;
typedef uint64_t EGLTime;
typedef intptr_t EGLAttrib;
#endif

/* ============================================================================
 * Availability and Initialization
 * ============================================================================ */

/**
 * Check if EGL 1.5 is available on the system
 * 
 * @return true if EGL 1.5 or higher is supported
 */
bool egl_v15_available(void);

/**
 * Initialize EGL 1.5 function pointers
 * Must be called before using any EGL 1.5 functions
 * 
 * @return true on success, false on failure
 */
bool egl_v15_init_functions(void);

/**
 * Check if EGL 1.5 is fully supported with all features
 * 
 * @param display EGL display to check
 * @return true if all EGL 1.5 features are available
 */
bool egl_v15_is_fully_supported(EGLDisplay display);

/* ============================================================================
 * Platform Display Creation
 * ============================================================================ */

/**
 * Get platform-specific EGL display (EGL 1.5 preferred method)
 * 
 * @param platform Platform type (EGL_PLATFORM_WAYLAND_KHR, etc.)
 * @param native_display Native display handle
 * @param attrib_list Optional attribute list (NULL for defaults)
 * @return EGL display, or EGL_NO_DISPLAY on failure
 */
EGLDisplay egl_v15_get_platform_display(EGLenum platform,
                                        void *native_display,
                                        const EGLAttrib *attrib_list);

/**
 * Get Wayland display (convenience wrapper)
 * 
 * @param wayland_display Wayland display handle
 * @return EGL display for Wayland, or EGL_NO_DISPLAY on failure
 */
EGLDisplay egl_v15_get_wayland_display(void *wayland_display);

/* ============================================================================
 * Platform Surface Creation
 * ============================================================================ */

/**
 * Create platform window surface (EGL 1.5 preferred method)
 * 
 * @param display EGL display
 * @param config EGL configuration
 * @param native_window Native window handle
 * @param attrib_list Optional attribute list (NULL for defaults)
 * @return EGL surface, or EGL_NO_SURFACE on failure
 */
EGLSurface egl_v15_create_platform_window_surface(EGLDisplay display,
                                                  EGLConfig config,
                                                  void *native_window,
                                                  const EGLAttrib *attrib_list);

/**
 * Create Wayland window surface (convenience wrapper)
 * 
 * @param display EGL display
 * @param config EGL configuration
 * @param wayland_window Wayland window handle (wl_egl_window)
 * @return EGL surface for Wayland window, or EGL_NO_SURFACE on failure
 */
EGLSurface egl_v15_create_wayland_window_surface(EGLDisplay display,
                                                 EGLConfig config,
                                                 void *wayland_window);

/* ============================================================================
 * Sync Objects - Core Operations
 * ============================================================================ */

/**
 * Create a sync object
 * 
 * @param display EGL display
 * @param type Sync type (EGL_SYNC_FENCE, etc.)
 * @param attrib_list Optional attribute list (NULL for defaults)
 * @return Sync object, or EGL_NO_SYNC on failure
 */
EGLSync egl_v15_create_sync(EGLDisplay display, EGLenum type, const EGLAttrib *attrib_list);

/**
 * Create fence sync object (most common use case)
 * Fence syncs are signaled when all previous GL commands complete
 * 
 * @param display EGL display
 * @return Fence sync object, or EGL_NO_SYNC on failure
 */
EGLSync egl_v15_create_fence_sync(EGLDisplay display);

/**
 * Destroy a sync object
 * 
 * @param display EGL display
 * @param sync Sync object to destroy
 * @return true on success, false on failure
 */
bool egl_v15_destroy_sync(EGLDisplay display, EGLSync sync);

/**
 * Wait for sync object on client side (blocks CPU thread)
 * 
 * @param display EGL display
 * @param sync Sync object to wait on
 * @param flags Wait flags (0 for default behavior)
 * @param timeout Timeout in nanoseconds (EGL_FOREVER for no timeout)
 * @return true if sync was signaled, false on timeout or error
 */
bool egl_v15_client_wait_sync(EGLDisplay display, EGLSync sync, EGLint flags, EGLTime timeout);

/**
 * Wait for sync object on GPU side (non-blocking for CPU)
 * GPU operations will wait until sync is signaled
 * 
 * @param display EGL display
 * @param sync Sync object to wait on
 * @param flags Wait flags (must be 0 for EGL 1.5)
 * @return true on success, false on failure
 */
bool egl_v15_wait_sync(EGLDisplay display, EGLSync sync, EGLint flags);

/**
 * Query sync object attributes
 * 
 * @param display EGL display
 * @param sync Sync object to query
 * @param attribute Attribute to query (EGL_SYNC_TYPE, EGL_SYNC_STATUS, etc.)
 * @param value Pointer to store attribute value
 * @return true on success, false on failure
 */
bool egl_v15_get_sync_attrib(EGLDisplay display, EGLSync sync, EGLint attribute, EGLAttrib *value);

/* ============================================================================
 * Sync Objects - Helper Functions
 * ============================================================================ */

/**
 * Check if sync object has been signaled
 * 
 * @param display EGL display
 * @param sync Sync object to check
 * @return true if signaled, false otherwise
 */
bool egl_v15_is_sync_signaled(EGLDisplay display, EGLSync sync);

/**
 * Wait for sync with timeout (convenience function)
 * 
 * @param display EGL display
 * @param sync Sync object to wait on
 * @param timeout_ns Timeout in nanoseconds
 * @return true if sync was signaled, false on timeout or error
 */
bool egl_v15_wait_sync_timeout(EGLDisplay display, EGLSync sync, uint64_t timeout_ns);

/**
 * Create fence, wait for it, then destroy (common pattern)
 * Useful for ensuring all GL commands complete
 * 
 * @param display EGL display
 * @param timeout_ns Timeout in nanoseconds
 * @return true if fence was signaled, false on timeout or error
 */
bool egl_v15_fence_and_wait(EGLDisplay display, uint64_t timeout_ns);

/**
 * Swap buffers with sync-based VSync
 * Creates a fence after swap to track completion
 * 
 * @param display EGL display
 * @param surface EGL surface to swap
 * @return true on success, false on failure
 */
bool egl_v15_vsync_with_sync(EGLDisplay display, EGLSurface surface);

/* ============================================================================
 * Information and Debugging
 * ============================================================================ */

/**
 * Get human-readable name for sync type
 * 
 * @param type Sync type enum value
 * @return Static string with sync type name
 */
const char *egl_v15_get_sync_type_name(EGLenum type);

/**
 * Print detailed sync object information to log
 * 
 * @param display EGL display
 * @param sync Sync object to print info about
 */
void egl_v15_print_sync_info(EGLDisplay display, EGLSync sync);

/**
 * Print detailed EGL 1.5 capabilities and information to log
 * 
 * @param display EGL display (or EGL_NO_DISPLAY to query default)
 */
void egl_v15_print_info(EGLDisplay display);

/* ============================================================================
 * Constants
 * ============================================================================ */

/* Common timeout values */
#define EGL_V15_TIMEOUT_NONE 0ULL
#define EGL_V15_TIMEOUT_1MS  1000000ULL
#define EGL_V15_TIMEOUT_16MS 16666666ULL  /* ~60 FPS frame time */
#define EGL_V15_TIMEOUT_33MS 33333333ULL  /* ~30 FPS frame time */
#define EGL_V15_TIMEOUT_1SEC 1000000000ULL

#ifndef EGL_FOREVER
#define EGL_FOREVER 0xFFFFFFFFFFFFFFFFULL
#endif

#endif /* EGL_V15_H */