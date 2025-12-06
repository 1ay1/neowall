#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/*
 * ============================================================================
 * COMPOSITOR ABSTRACTION LAYER
 * ============================================================================
 *
 * This abstraction layer allows NeoWall to work with ANY Wayland compositor
 * by providing a unified interface for wallpaper surface management.
 *
 * DESIGN GOALS:
 * 1. Backend-agnostic API - same interface for all compositors
 * 2. Runtime detection - automatically select correct backend
 * 3. Extensible - easy to add new compositor backends
 * 4. Zero overhead - direct function pointers, no indirection in hot paths
 *
 * SUPPORTED BACKENDS:
 * - wlroots-based (Hyprland, Sway, River, etc.) - via wlr-layer-shell
 * - KDE Plasma - via org_kde_plasma_shell
 * - GNOME Shell/Mutter - via fallback subsurface method
 * - Generic fallback - works on any compositor (limited features)
 *
 * ADDING NEW BACKENDS:
 * 1. Implement compositor_backend_ops in src/compositor/backends/
 * 2. Register backend in compositor_registry.c
 * 3. Backend is auto-detected and loaded at runtime
 */

/* Forward declarations */
struct neowall_state;
struct compositor_backend;
struct compositor_surface;

/* ============================================================================
 * COMPOSITOR CAPABILITIES
 * ============================================================================ */

/**
 * Compositor capability flags
 * Indicates which features the backend supports
 */
typedef enum {
    COMPOSITOR_CAP_NONE             = 0,
    COMPOSITOR_CAP_LAYER_SHELL      = (1 << 0),  /* Supports layer shell (background layer) */
    COMPOSITOR_CAP_SUBSURFACES      = (1 << 1),  /* Supports subsurfaces */
    COMPOSITOR_CAP_VIEWPORT         = (1 << 2),  /* Supports wp_viewporter */
    COMPOSITOR_CAP_EXCLUSIVE_ZONE   = (1 << 3),  /* Supports exclusive zones */
    COMPOSITOR_CAP_KEYBOARD_INTERACTIVITY = (1 << 4),  /* Can disable keyboard input */
    COMPOSITOR_CAP_ANCHOR           = (1 << 5),  /* Supports surface anchoring */
    COMPOSITOR_CAP_MULTI_OUTPUT     = (1 << 6),  /* Can bind surfaces to specific outputs */
} compositor_capabilities_t;

/* ============================================================================
 * SURFACE CONFIGURATION
 * ============================================================================ */

/**
 * Surface layer - where the wallpaper should be placed in the compositor's stack
 */
typedef enum {
    COMPOSITOR_LAYER_BACKGROUND = 0,   /* Below everything (wallpaper) */
    COMPOSITOR_LAYER_BOTTOM,           /* Above background, below windows */
    COMPOSITOR_LAYER_TOP,              /* Above windows, below overlays */
    COMPOSITOR_LAYER_OVERLAY,          /* Top-most layer */
} compositor_layer_t;

/**
 * Surface anchor flags - how surface is positioned on output
 */
typedef enum {
    COMPOSITOR_ANCHOR_TOP    = (1 << 0),
    COMPOSITOR_ANCHOR_BOTTOM = (1 << 1),
    COMPOSITOR_ANCHOR_LEFT   = (1 << 2),
    COMPOSITOR_ANCHOR_RIGHT  = (1 << 3),
} compositor_anchor_t;

#define COMPOSITOR_ANCHOR_FILL (COMPOSITOR_ANCHOR_TOP | COMPOSITOR_ANCHOR_BOTTOM | \
                               COMPOSITOR_ANCHOR_LEFT | COMPOSITOR_ANCHOR_RIGHT)

/**
 * Surface configuration parameters
 */
typedef struct {
    compositor_layer_t layer;           /* Which layer to place surface on */
    uint32_t anchor;                    /* Anchor flags (compositor_anchor_t) */
    int32_t exclusive_zone;             /* Exclusive zone size (-1 = auto, 0 = none) */
    bool keyboard_interactivity;        /* Whether surface accepts keyboard input */
    int32_t width;                      /* Desired width (0 = auto) */
    int32_t height;                     /* Desired height (0 = auto) */
    struct wl_output *output;           /* Target output (NULL = all outputs) */
} compositor_surface_config_t;

/* ============================================================================
 * COMPOSITOR SURFACE
 * ============================================================================ */

/**
 * Compositor surface - opaque handle to a wallpaper surface
 * 
 * This wraps compositor-specific surface types and provides a unified interface.
 * Backend implementations store their protocol-specific data in opaque_data.
 */
struct compositor_surface {
    struct wl_surface *wl_surface;      /* Base Wayland surface */
    struct wl_egl_window *egl_window;   /* EGL window for rendering */
    EGLSurface egl_surface;             /* EGL surface */
    
    struct wl_output *output;           /* Associated output */
    int32_t width;                      /* Current surface width */
    int32_t height;                     /* Current surface height */
    int32_t scale;                      /* Output scale factor */
    
    compositor_surface_config_t config; /* Surface configuration */
    bool configured;                    /* Has surface been configured? */
    bool committed;                     /* Has initial commit been done? */
    
    void *backend_data;                 /* Backend-specific data (opaque) */
    struct compositor_backend *backend; /* Back-pointer to backend */
    
    struct wp_tearing_control_v1 *tearing_control; /* Tearing control for immediate presentation */
    
    /* Callbacks */
    void (*on_configure)(struct compositor_surface *surface, 
                        int32_t width, int32_t height);
    void (*on_closed)(struct compositor_surface *surface);
    void *user_data;                    /* User data for callbacks */
};

/* ============================================================================
 * COMPOSITOR BACKEND OPERATIONS
 * ============================================================================ */

/**
 * Backend operations - function pointers implemented by each backend
 * 
 * All backends MUST implement these operations. If an operation is not
 * supported, it should return false or NULL and log an appropriate message.
 */
typedef struct compositor_backend_ops {
    /**
     * Initialize backend - called once during compositor initialization
     * 
     * @param state Global NeoWall state
     * @return Backend-specific data pointer, or NULL on failure
     */
    void *(*init)(struct neowall_state *state);
    
    /**
     * Cleanup backend - called during shutdown
     * 
     * @param backend_data Data returned from init()
     */
    void (*cleanup)(void *backend_data);
    
    /**
     * Create a wallpaper surface
     * 
     * @param backend_data Data returned from init()
     * @param config Surface configuration
     * @return Compositor surface, or NULL on failure
     */
    struct compositor_surface *(*create_surface)(void *backend_data,
                                                 const compositor_surface_config_t *config);
    
    /**
     * Destroy a wallpaper surface
     * 
     * @param surface Surface to destroy
     */
    void (*destroy_surface)(struct compositor_surface *surface);
    
    /**
     * Configure surface parameters (size, layer, etc.)
     * 
     * @param surface Surface to configure
     * @param config New configuration
     * @return true on success, false on failure
     */
    bool (*configure_surface)(struct compositor_surface *surface,
                             const compositor_surface_config_t *config);
    
    /**
     * Commit surface changes to compositor
     * 
     * @param surface Surface to commit
     */
    void (*commit_surface)(struct compositor_surface *surface);
    
    /**
     * Create EGL window for surface
     * 
     * @param surface Surface to create EGL window for
     * @param width Window width
     * @param height Window height
     * @return true on success, false on failure
     */
    bool (*create_egl_window)(struct compositor_surface *surface,
                             int32_t width, int32_t height);
    
    /**
     * Destroy EGL window for surface
     * 
     * @param surface Surface whose EGL window to destroy
     */
    void (*destroy_egl_window)(struct compositor_surface *surface);
    
    /**
     * Get backend capabilities
     * 
     * @param backend_data Data returned from init()
     * @return Capability flags (compositor_capabilities_t)
     */
    compositor_capabilities_t (*get_capabilities)(void *backend_data);
    
    /**
     * Handle output added event (optional)
     * 
     * @param backend_data Data returned from init()
     * @param output New output
     */
    void (*on_output_added)(void *backend_data, struct wl_output *output);
    
    /**
     * Handle output removed event (optional)
     * 
     * @param backend_data Data returned from init()
     * @param output Removed output
     */
    void (*on_output_removed)(void *backend_data, struct wl_output *output);
    
    /**
     * Initialize outputs for this backend (optional)
     * Called when no Wayland outputs are available (X11 backend)
     * 
     * @param backend_data Data returned from init()
     * @param state Global state
     * @return true on success, false on failure
     */
    bool (*init_outputs)(void *backend_data, struct neowall_state *state);
    
    /* ===== EVENT HANDLING OPERATIONS ===== */
    
    /**
     * Get file descriptor for polling
     * Returns the file descriptor that can be polled for compositor events.
     * 
     * @param backend_data Data returned from init()
     * @return FD for poll(), or -1 if not pollable
     */
    int (*get_fd)(void *backend_data);
    
    /**
     * Prepare for event reading
     * Prepares the backend for reading events (Wayland-specific, no-op for X11).
     * Must be called before poll() for Wayland backends.
     * 
     * @param backend_data Data returned from init()
     * @return true on success, false on failure
     */
    bool (*prepare_events)(void *backend_data);
    
    /**
     * Read events from compositor
     * Reads events from the compositor (may block if prepared).
     * For Wayland, reads events that were prepared. For X11, this is a no-op.
     * 
     * @param backend_data Data returned from init()
     * @return true on success, false on failure
     */
    bool (*read_events)(void *backend_data);
    
    /**
     * Dispatch pending events
     * Processes all pending events from the compositor.
     * 
     * @param backend_data Data returned from init()
     * @return true on success, false on failure
     */
    bool (*dispatch_events)(void *backend_data);
    
    /**
     * Flush outgoing requests to compositor
     * Sends all buffered requests to the compositor.
     * 
     * @param backend_data Data returned from init()
     * @return true on success, false on failure (EAGAIN/EPIPE are failures)
     */
    bool (*flush)(void *backend_data);
    
    /**
     * Cancel prepared event read
     * Cancels a prepared read operation (Wayland-specific, no-op for X11).
     * Must be called if prepare_events() was called but read_events() won't be.
     * 
     * @param backend_data Data returned from init()
     */
    void (*cancel_read)(void *backend_data);
    
    /**
     * Get error code from display/connection
     * Returns the error code from the compositor connection.
     * 
     * @param backend_data Data returned from init()
     * @return 0 if no error, error code otherwise
     */
    int (*get_error)(void *backend_data);
    
    /**
     * Synchronize with display server
     * Flushes all pending requests and waits for the server to process them.
     * This is equivalent to wl_display_roundtrip() for Wayland or XSync() for X11.
     * 
     * @param backend_data Data returned from init()
     * @return true on success, false on failure
     */
    bool (*sync)(void *backend_data);
    
    /* ===== DISPLAY/EGL OPERATIONS ===== */
    
    /**
     * Get native display handle for EGL
     * Returns the native display pointer for EGL initialization.
     * 
     * @param backend_data Data returned from init()
     * @return Native display pointer (wl_display* or Display*)
     */
    void *(*get_native_display)(void *backend_data);
    
    /**
     * Get EGL platform type
     * Returns the EGL platform type for this backend.
     * 
     * @param backend_data Data returned from init()
     * @return EGL_PLATFORM_WAYLAND_KHR, EGL_PLATFORM_X11_KHR, etc.
     */
    EGLenum (*get_egl_platform)(void *backend_data);
} compositor_backend_ops_t;

/* ============================================================================
 * COMPOSITOR BACKEND
 * ============================================================================ */

/**
 * Compositor backend - represents a compositor-specific implementation
 */
struct compositor_backend {
    const char *name;                   /* Backend name (e.g., "wlr-layer-shell") */
    const char *description;            /* Human-readable description */
    int priority;                       /* Selection priority (higher = preferred) */
    
    const compositor_backend_ops_t *ops; /* Backend operations */
    void *data;                         /* Backend-specific data */
    
    compositor_capabilities_t capabilities; /* Cached capabilities */
};

/* ============================================================================
 * COMPOSITOR DETECTION
 * ============================================================================ */

/**
 * Compositor type - detected compositor
 */
typedef enum {
    COMPOSITOR_TYPE_UNKNOWN = 0,
    COMPOSITOR_TYPE_HYPRLAND,
    COMPOSITOR_TYPE_SWAY,
    COMPOSITOR_TYPE_RIVER,
    COMPOSITOR_TYPE_WAYFIRE,
    COMPOSITOR_TYPE_KDE_PLASMA,
    COMPOSITOR_TYPE_GNOME_SHELL,
    COMPOSITOR_TYPE_MUTTER,
    COMPOSITOR_TYPE_WESTON,
    COMPOSITOR_TYPE_GENERIC,            /* Generic wlroots-based */
} compositor_type_t;

/**
 * Compositor detection info
 */
typedef struct {
    compositor_type_t type;
    const char *name;
    const char *version;
    bool has_layer_shell;               /* wlr-layer-shell-v1 available */
    bool has_kde_shell;                 /* org_kde_plasma_shell available */
    bool has_gtk_shell;                 /* gtk_shell1 available */
} compositor_info_t;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * Detect which compositor is running
 * 
 * @param display Wayland display
 * @return Compositor info
 */
compositor_info_t compositor_detect(struct wl_display *display);

/**
 * Initialize compositor backend
 * Auto-detects compositor and selects best backend.
 * 
 * @param state Global NeoWall state
 * @return Compositor backend, or NULL on failure
 */
struct compositor_backend *compositor_backend_init(struct neowall_state *state);

/**
 * Cleanup compositor backend
 * 
 * @param backend Backend to cleanup
 */
void compositor_backend_cleanup(struct compositor_backend *backend);

/**
 * Create a wallpaper surface
 * 
 * @param backend Compositor backend
 * @param config Surface configuration
 * @return Compositor surface, or NULL on failure
 */
struct compositor_surface *compositor_surface_create(struct compositor_backend *backend,
                                                     const compositor_surface_config_t *config);

/**
 * Destroy a wallpaper surface
 * 
 * @param surface Surface to destroy
 */
void compositor_surface_destroy(struct compositor_surface *surface);

/**
 * Configure surface parameters
 * 
 * @param surface Surface to configure
 * @param config New configuration
 * @return true on success, false on failure
 */
bool compositor_surface_configure(struct compositor_surface *surface,
                                  const compositor_surface_config_t *config);

/**
 * Damage surface region
 * Marks a region of the surface as damaged and needing repaint.
 * 
 * @param surface Surface to damage
 * @param x X coordinate of damaged region
 * @param y Y coordinate of damaged region
 * @param width Width of damaged region
 * @param height Height of damaged region
 */
void compositor_surface_damage(struct compositor_surface *surface,
                               int32_t x, int32_t y,
                               int32_t width, int32_t height);

/**
 * Commit surface changes
 * 
 * @param surface Surface to commit
 */
void compositor_surface_commit(struct compositor_surface *surface);

/**
 * Create EGL window for rendering
 * 
 * @param surface Surface to create window for
 * @param egl_display EGL display
 * @param width Window width
 * @param height Window height
 * @return EGL surface, or EGL_NO_SURFACE on failure
 */
EGLSurface compositor_surface_create_egl(struct compositor_surface *surface,
                                         EGLDisplay egl_display,
                                         EGLConfig egl_config,
                                         int32_t width, int32_t height);

/**
 * Destroy EGL window
 * 
 * @param surface Surface whose EGL window to destroy
 * @param egl_display EGL display
 */
void compositor_surface_destroy_egl(struct compositor_surface *surface,
                                    EGLDisplay egl_display);

/**
 * Get backend capabilities
 * 
 * @param backend Compositor backend
 * @return Capability flags
 */
compositor_capabilities_t compositor_backend_get_capabilities(struct compositor_backend *backend);

/**
 * Get compositor type name as string
 * 
 * @param type Compositor type
 * @return String name
 */
const char *compositor_type_to_string(compositor_type_t type);

/**
 * Get default surface configuration
 * 
 * @param output Target output (or NULL for all outputs)
 * @return Default configuration
 */
compositor_surface_config_t compositor_surface_config_default(struct wl_output *output);

/**
 * Check if surface is ready for rendering
 * 
 * @param surface Surface to check
 * @return true if ready, false otherwise
 */
bool compositor_surface_is_ready(struct compositor_surface *surface);

/**
 * Get surface dimensions
 * 
 * @param surface Surface to query
 * @param width Output parameter for width (can be NULL)
 * @param height Output parameter for height (can be NULL)
 */
void compositor_surface_get_size(struct compositor_surface *surface,
                                int32_t *width, int32_t *height);

/**
 * Resize EGL window
 * 
 * @param surface Surface to resize
 * @param width New width
 * @param height New height
 * @return true on success, false on failure
 */
bool compositor_surface_resize_egl(struct compositor_surface *surface,
                                   int32_t width, int32_t height);

/**
 * Set surface scale factor
 * 
 * @param surface Surface to configure
 * @param scale Scale factor (typically 1, 2, 3, etc.)
 */
void compositor_surface_set_scale(struct compositor_surface *surface, int32_t scale);

/**
 * Set surface callbacks
 * 
 * @param surface Surface to configure
 * @param on_configure Callback for configure events
 * @param on_closed Callback for close events
 * @param user_data User data passed to callbacks
 */
void compositor_surface_set_callbacks(struct compositor_surface *surface,
                                     void (*on_configure)(struct compositor_surface*, int32_t, int32_t),
                                     void (*on_closed)(struct compositor_surface*),
                                     void *user_data);

/* ============================================================================
 * BACKEND REGISTRATION (for backend implementations)
 * ============================================================================ */

/**
 * Backend registration function signature
 * Each backend implements this to register itself.
 */
typedef struct compositor_backend *(*compositor_backend_register_fn)(struct neowall_state *state);

/**
 * Register a backend (called by backend implementations during init)
 * 
 * @param name Backend name
 * @param description Backend description
 * @param priority Selection priority
 * @param ops Backend operations
 * @return true on success
 */
bool compositor_backend_register(const char *name,
                                 const char *description,
                                 int priority,
                                 const compositor_backend_ops_t *ops);

/* ============================================================================
 * BACKEND IMPLEMENTATIONS (to be implemented in backends/)
 * ============================================================================ */

/* wlr-layer-shell backend (wlroots-based compositors) */
struct compositor_backend *compositor_backend_wlr_layer_shell_init(struct neowall_state *state);

/* KDE Plasma Shell backend */
struct compositor_backend *compositor_backend_kde_plasma_init(struct neowall_state *state);

/* GNOME Shell backend (subsurface fallback) */
struct compositor_backend *compositor_backend_gnome_shell_init(struct neowall_state *state);

/* Generic fallback backend (works everywhere, limited features) */
struct compositor_backend *compositor_backend_fallback_init(struct neowall_state *state);

#endif /* COMPOSITOR_H */