#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <wayland-client.h>
#include "compositor.h"
#include "neowall.h"

/*
 * ============================================================================
 * COMPOSITOR BACKEND REGISTRY
 * ============================================================================
 *
 * This module handles:
 * 1. Compositor detection - identify which compositor is running
 * 2. Backend registration - backends register themselves at startup
 * 3. Backend selection - choose the best backend for current compositor
 * 4. Protocol detection - scan available Wayland protocols
 *
 * BACKEND PRIORITY SYSTEM:
 * - Higher priority = preferred backend
 * - wlr-layer-shell: 100 (best for wlroots compositors)
 * - KDE Plasma: 90 (native KDE support)
 * - GNOME Shell: 80 (GNOME-specific)
 * - Fallback: 10 (works everywhere, limited features)
 */

/* Maximum number of backends that can be registered */
#define MAX_BACKENDS 16

/* Backend registry */
static struct {
    const char *name;
    const char *description;
    int priority;
    const compositor_backend_ops_t *ops;
} backend_registry[MAX_BACKENDS];

static size_t backend_count = 0;

/* Protocol detection state */
typedef struct {
    bool has_layer_shell;       /* zwlr_layer_shell_v1 */
    bool has_kde_shell;         /* org_kde_plasma_shell */
    bool has_gtk_shell;         /* gtk_shell1 */
    bool has_viewporter;        /* wp_viewporter */
    char compositor_name[256];  /* XDG_CURRENT_DESKTOP or detected name */
} protocol_state_t;

/* ============================================================================
 * PROTOCOL DETECTION
 * ============================================================================ */

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
    protocol_state_t *state = data;
    
    (void)registry;
    (void)name;
    (void)version;
    
    if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        state->has_layer_shell = true;
        log_debug("Detected protocol: zwlr_layer_shell_v1");
    } else if (strcmp(interface, "org_kde_plasma_shell") == 0) {
        state->has_kde_shell = true;
        log_debug("Detected protocol: org_kde_plasma_shell");
    } else if (strcmp(interface, "gtk_shell1") == 0) {
        state->has_gtk_shell = true;
        log_debug("Detected protocol: gtk_shell1");
    } else if (strcmp(interface, "wp_viewporter") == 0) {
        state->has_viewporter = true;
        log_debug("Detected protocol: wp_viewporter");
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
                                         uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
    /* Nothing to do */
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

/* Detect available Wayland protocols */
static protocol_state_t detect_protocols(struct wl_display *display) {
    protocol_state_t state = {0};
    
    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        log_error("Failed to get Wayland registry");
        return state;
    }
    
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    
    return state;
}

/* ============================================================================
 * COMPOSITOR TYPE DETECTION
 * ============================================================================ */

static compositor_type_t detect_compositor_type(const protocol_state_t *proto) {
    /* Check environment variables first */
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_DESKTOP");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    
    /* Hyprland detection */
    if ((desktop && strstr(desktop, "Hyprland")) ||
        (session && strstr(session, "Hyprland")) ||
        (wayland_display && strstr(wayland_display, "hyprland"))) {
        log_info("Detected compositor: Hyprland");
        return COMPOSITOR_TYPE_HYPRLAND;
    }
    
    /* Sway detection */
    if ((desktop && strstr(desktop, "sway")) ||
        (session && strstr(session, "sway")) ||
        getenv("SWAYSOCK")) {
        log_info("Detected compositor: Sway");
        return COMPOSITOR_TYPE_SWAY;
    }
    
    /* River detection */
    if ((desktop && strstr(desktop, "river")) ||
        (session && strstr(session, "river"))) {
        log_info("Detected compositor: River");
        return COMPOSITOR_TYPE_RIVER;
    }
    
    /* Wayfire detection */
    if ((desktop && strstr(desktop, "wayfire")) ||
        (session && strstr(session, "wayfire"))) {
        log_info("Detected compositor: Wayfire");
        return COMPOSITOR_TYPE_WAYFIRE;
    }
    
    /* KDE Plasma detection */
    if ((desktop && strstr(desktop, "KDE")) ||
        (session && strstr(session, "plasma")) ||
        proto->has_kde_shell) {
        log_info("Detected compositor: KDE Plasma");
        return COMPOSITOR_TYPE_KDE_PLASMA;
    }
    
    /* GNOME Shell detection */
    if ((desktop && strstr(desktop, "GNOME")) ||
        (session && strstr(session, "gnome")) ||
        proto->has_gtk_shell) {
        log_info("Detected compositor: GNOME Shell");
        return COMPOSITOR_TYPE_GNOME_SHELL;
    }
    
    /* Mutter detection (GNOME's compositor) */
    if (session && strstr(session, "mutter")) {
        log_info("Detected compositor: Mutter");
        return COMPOSITOR_TYPE_MUTTER;
    }
    
    /* Weston detection */
    if ((desktop && strstr(desktop, "weston")) ||
        (session && strstr(session, "weston"))) {
        log_info("Detected compositor: Weston");
        return COMPOSITOR_TYPE_WESTON;
    }
    
    /* Generic wlroots-based if layer shell is available */
    if (proto->has_layer_shell) {
        log_info("Detected compositor: Generic wlroots-based");
        return COMPOSITOR_TYPE_GENERIC;
    }
    
    /* Unknown compositor */
    log_info("Detected compositor: Unknown");
    return COMPOSITOR_TYPE_UNKNOWN;
}

/* Get compositor name string */
const char *compositor_type_to_string(compositor_type_t type) {
    switch (type) {
        case COMPOSITOR_TYPE_HYPRLAND:    return "Hyprland";
        case COMPOSITOR_TYPE_SWAY:        return "Sway";
        case COMPOSITOR_TYPE_RIVER:       return "River";
        case COMPOSITOR_TYPE_WAYFIRE:     return "Wayfire";
        case COMPOSITOR_TYPE_KDE_PLASMA:  return "KDE Plasma";
        case COMPOSITOR_TYPE_GNOME_SHELL: return "GNOME Shell";
        case COMPOSITOR_TYPE_MUTTER:      return "Mutter";
        case COMPOSITOR_TYPE_WESTON:      return "Weston";
        case COMPOSITOR_TYPE_GENERIC:     return "Generic wlroots";
        case COMPOSITOR_TYPE_UNKNOWN:
        default:                          return "Unknown";
    }
}

/* Public API: Detect compositor */
compositor_info_t compositor_detect(struct wl_display *display) {
    compositor_info_t info = {0};
    
    /* Detect protocols */
    protocol_state_t proto = detect_protocols(display);
    
    /* Detect compositor type */
    info.type = detect_compositor_type(&proto);
    info.name = compositor_type_to_string(info.type);
    info.has_layer_shell = proto.has_layer_shell;
    info.has_kde_shell = proto.has_kde_shell;
    info.has_gtk_shell = proto.has_gtk_shell;
    
    /* Try to get version from environment */
    const char *version = getenv("COMPOSITOR_VERSION");
    info.version = version ? version : "unknown";
    
    return info;
}

/* ============================================================================
 * BACKEND REGISTRATION
 * ============================================================================ */

bool compositor_backend_register(const char *name,
                                 const char *description,
                                 int priority,
                                 const compositor_backend_ops_t *ops) {
    if (backend_count >= MAX_BACKENDS) {
        log_error("Backend registry full, cannot register '%s'", name);
        return false;
    }
    
    if (!name || !ops) {
        log_error("Invalid backend registration parameters");
        return false;
    }
    
    /* Check if backend already registered */
    for (size_t i = 0; i < backend_count; i++) {
        if (strcmp(backend_registry[i].name, name) == 0) {
            log_error("Backend '%s' already registered", name);
            return false;
        }
    }
    
    /* Register backend */
    backend_registry[backend_count].name = name;
    backend_registry[backend_count].description = description;
    backend_registry[backend_count].priority = priority;
    backend_registry[backend_count].ops = ops;
    backend_count++;
    
    log_debug("Registered backend: %s (priority: %d) - %s", 
              name, priority, description);
    
    return true;
}

/* ============================================================================
 * BACKEND SELECTION
 * ============================================================================ */

/* Select best backend based on compositor and available protocols */
static struct compositor_backend *select_backend(struct neowall_state *state,
                                                 const compositor_info_t *info) {
    struct compositor_backend *best_backend = NULL;
    int best_priority = -1;
    
    log_info("Selecting backend for %s compositor...", info->name);
    
    /* Try each registered backend */
    for (size_t i = 0; i < backend_count; i++) {
        const char *name = backend_registry[i].name;
        int priority = backend_registry[i].priority;
        const compositor_backend_ops_t *ops = backend_registry[i].ops;
        
        log_debug("Trying backend: %s (priority: %d)", name, priority);
        
        /* Try to initialize backend */
        void *backend_data = ops->init(state);
        if (!backend_data) {
            log_debug("Backend '%s' initialization failed", name);
            continue;
        }
        
        /* Check if this backend has higher priority */
        if (priority > best_priority) {
            /* Clean up previous best backend if any */
            if (best_backend && best_backend->ops->cleanup) {
                best_backend->ops->cleanup(best_backend->data);
                free(best_backend);
            }
            
            /* Allocate new backend */
            best_backend = calloc(1, sizeof(struct compositor_backend));
            if (!best_backend) {
                log_error("Failed to allocate backend structure");
                ops->cleanup(backend_data);
                continue;
            }
            
            /* Initialize backend structure */
            best_backend->name = name;
            best_backend->description = backend_registry[i].description;
            best_backend->priority = priority;
            best_backend->ops = ops;
            best_backend->data = backend_data;
            best_backend->capabilities = ops->get_capabilities(backend_data);
            
            best_priority = priority;
            
            log_info("Selected backend: %s", name);
        } else {
            /* This backend has lower priority, clean it up */
            ops->cleanup(backend_data);
        }
    }
    
    if (!best_backend) {
        log_error("No suitable backend found for compositor: %s", info->name);
    }
    
    return best_backend;
}

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================ */

/* Initialize compositor backend */
struct compositor_backend *compositor_backend_init(struct neowall_state *state) {
    if (!state || !state->display) {
        log_error("Invalid state for compositor backend initialization");
        return NULL;
    }
    
    /* Detect compositor */
    compositor_info_t info = compositor_detect(state->display);
    
    log_info("Compositor: %s", info.name);
    log_info("Layer shell support: %s", info.has_layer_shell ? "yes" : "no");
    log_info("KDE shell support: %s", info.has_kde_shell ? "yes" : "no");
    log_info("GTK shell support: %s", info.has_gtk_shell ? "yes" : "no");
    
    /* Register all available backends */
    log_debug("Registering available backends...");
    
    /* These will be implemented in separate backend files */
    compositor_backend_wlr_layer_shell_init(state);
    compositor_backend_kde_plasma_init(state);
    compositor_backend_gnome_shell_init(state);
    compositor_backend_fallback_init(state);
    
    /* Select best backend */
    struct compositor_backend *backend = select_backend(state, &info);
    
    if (backend) {
        log_info("Using backend: %s - %s", backend->name, backend->description);
        log_info("Backend capabilities: 0x%08x", backend->capabilities);
    } else {
        log_error("Failed to initialize any compositor backend");
    }
    
    return backend;
}

/* Cleanup compositor backend */
void compositor_backend_cleanup(struct compositor_backend *backend) {
    if (!backend) {
        return;
    }
    
    log_debug("Cleaning up compositor backend: %s", backend->name);
    
    if (backend->ops && backend->ops->cleanup && backend->data) {
        backend->ops->cleanup(backend->data);
    }
    
    free(backend);
}

/* Get backend capabilities */
compositor_capabilities_t compositor_backend_get_capabilities(struct compositor_backend *backend) {
    if (!backend) {
        return COMPOSITOR_CAP_NONE;
    }
    
    return backend->capabilities;
}
