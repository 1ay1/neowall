/*
 * macOS AppKit compositor backend — "desktop window" wallpaper.
 *
 * Native model (same as Plash / professional wallpaper apps):
 *   - One borderless NSWindow per NSScreen at kCGDesktopWindowLevel, i.e.
 *     ABOVE the system wallpaper but BELOW desktop icons and all windows.
 *   - collectionBehavior CanJoinAllSpaces|Stationary|IgnoresCycle: the
 *     wallpaper follows every Space/fullscreen transition and never appears
 *     in Mission Control or the window cycle.
 *   - ignoresMouseEvents: clicks pass through to the desktop icons.
 *   - NSApplicationActivationPolicyAccessory: no Dock icon, no menu bar.
 *   - NSWindowOcclusionState drives the engine's occlusion pause, so a
 *     fullscreen app suspends rendering exactly like on Hyprland.
 *   - CGDisplay reconfiguration callback drives hotplug (screen add/remove).
 *
 * Threading: AppKit demands window work on the main thread. main() calls
 * macos_platform_init() FIRST, which transforms the process into an
 * accessory app and pumps NSApplication on the MAIN thread, while the
 * engine's poll() event loop runs on a secondary thread. All AppKit
 * mutations from the engine thread hop through dispatch_sync(main_queue).
 *
 * The engine's poll loop needs a pollable fd for "compositor events"; we
 * provide a self-pipe that the display-reconfig callback writes to, and the
 * engine re-enumerates screens when it fires (get_fd/dispatch_events ops).
 */
#ifdef __APPLE__

#define GL_SILENCE_DEPRECATION 1

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include "neowall/neowall.h"
#include "neowall/compositor/compositor.h"
#include "neowall/output/output.h"
#include "neowall/config/config.h"
#include "neowall/platform/egl_compat.h"

#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

/* ============================================================================
 * Backend data
 * ========================================================================== */

typedef struct macos_backend_data {
    struct neowall_state *state;
    int pipe_read;   /* engine polls this */
    int pipe_write;  /* display-reconfig callback writes this */
    bool initialized;
} macos_backend_data_t;

typedef struct macos_surface_data {
    NSWindow *window;
    NSView *view;
    NSScreen *screen;
    CGDirectDisplayID display_id;
} macos_surface_data_t;

static macos_backend_data_t *g_backend; /* for the C reconfig callback */

/* ============================================================================
 * Main-thread helper
 * ========================================================================== */

static void run_on_main(void (^block)(void)) {
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

/* ============================================================================
 * NSApplication bootstrap (called from main() before the engine starts)
 * ========================================================================== */

static atomic_bool g_app_running;

void macos_platform_init(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        /* Accessory: no Dock icon, no menu bar — it's a daemon with windows. */
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        atomic_store(&g_app_running, true);
    }
}

/* Pump pending AppKit events without blocking. The engine thread calls this
 * via dispatch; in practice occlusion/reconfig notifications arrive on the
 * main runloop, which dispatch_get_main_queue() services because main()
 * parks itself in macos_platform_run() below. */
void macos_platform_run(void) {
    /* Parks the main thread in the Cocoa runloop forever. The engine event
     * loop runs on its own thread; when it exits it calls
     * macos_platform_stop() which terminates this loop. */
    @autoreleasepool {
        [NSApp run];
    }
}

void macos_platform_stop(void) {
    if (!atomic_load(&g_app_running)) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
        /* [NSApp stop:] only takes effect after an event arrives. */
        NSEvent *wake = [NSEvent
            otherEventWithType:NSEventTypeApplicationDefined
                      location:NSMakePoint(0, 0)
                 modifierFlags:0
                     timestamp:0
                  windowNumber:0
                       context:nil
                       subtype:0
                         data1:0
                         data2:0];
        [NSApp postEvent:wake atStart:YES];
    });
}

/* ============================================================================
 * Display reconfiguration -> engine wakeup
 * ========================================================================== */

static void display_reconfig_cb(CGDirectDisplayID display,
                                CGDisplayChangeSummaryFlags flags,
                                void *user) {
    (void)display;
    (void)user;
    if (!(flags & (kCGDisplayAddFlag | kCGDisplayRemoveFlag |
                   kCGDisplaySetModeFlag | kCGDisplayDesktopShapeChangedFlag))) {
        return;
    }
    if (g_backend && g_backend->pipe_write >= 0) {
        uint8_t b = 1;
        ssize_t s = write(g_backend->pipe_write, &b, 1);
        (void)s;
    }
}

/* ============================================================================
 * Occlusion observer — pauses rendering under fullscreen apps natively
 * ========================================================================== */

@interface NWOcclusionObserver : NSObject
@property(assign) struct output_state *output;
@end

@implementation NWOcclusionObserver
- (void)occlusionChanged:(NSNotification *)note {
    NSWindow *win = note.object;
    bool visible = ([win occlusionState] & NSWindowOcclusionStateVisible) != 0;
    if (self.output) {
        atomic_store_explicit(&self.output->occluded, !visible,
                              memory_order_release);
        if (visible) {
            atomic_store_explicit(&self.output->needs_redraw, true,
                                  memory_order_release);
        }
    }
}
@end

/* ============================================================================
 * Surface ops
 * ========================================================================== */

static const void *kNWOcclusionKey = &kNWOcclusionKey;

static struct compositor_surface *
macos_create_surface(void *backend_data, const compositor_surface_config_t *config) {
    macos_backend_data_t *backend = backend_data;
    if (!backend || !config) return NULL;

    NSScreen *screen = (__bridge NSScreen *)config->output;

    struct compositor_surface *surface = calloc(1, sizeof(*surface));
    macos_surface_data_t *data = calloc(1, sizeof(*data));
    if (!surface || !data) {
        free(surface);
        free(data);
        return NULL;
    }

    __block NSWindow *window = nil;
    __block NSView *view = nil;
    __block int pix_w = 0, pix_h = 0;
    __block int scale = 1;

    run_on_main(^{
        NSScreen *target = screen ?: [NSScreen mainScreen];
        NSRect frame = [target frame];

        window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:NSWindowStyleMaskBorderless
                                               backing:NSBackingStoreBuffered
                                                 defer:NO
                                                screen:target];
        /* Desktop level: above system wallpaper, below icons and windows. */
        [window setLevel:kCGDesktopWindowLevel];
        [window setCollectionBehavior:
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorStationary |
            NSWindowCollectionBehaviorIgnoresCycle];
        [window setOpaque:YES];
        [window setBackgroundColor:[NSColor blackColor]];
        [window setIgnoresMouseEvents:YES];
        [window setHasShadow:NO];
        [window setReleasedWhenClosed:NO];
        [window setDisplaysWhenScreenProfileChanges:YES];

        view = [[NSView alloc] initWithFrame:
            NSMakeRect(0, 0, frame.size.width, frame.size.height)];
        [view setWantsBestResolutionOpenGLSurface:YES];
        [window setContentView:view];
        [window orderBack:nil]; /* show, at the back of its level */

        NSRect backing = [view convertRectToBacking:[view bounds]];
        pix_w = (int)backing.size.width;
        pix_h = (int)backing.size.height;
        scale = (int)[target backingScaleFactor];
        if (scale < 1) scale = 1;
    });

    data->window = window;
    data->view = view;
    data->screen = screen;
    NSDictionary *desc = [(screen ?: [NSScreen mainScreen]) deviceDescription];
    data->display_id =
        (CGDirectDisplayID)[desc[@"NSScreenNumber"] unsignedIntValue];

    surface->backend_data = data;
    surface->native_surface = (__bridge void *)view;
    surface->native_output = config->output;
    surface->width = pix_w;
    surface->height = pix_h;
    surface->scale = scale;
    surface->config = *config;
    surface->configured = true;
    surface->committed = true;
    surface->egl_surface = EGL_NO_SURFACE;

    log_info("macOS: created desktop window %dx%d @%dx (display %u)",
             pix_w, pix_h, scale, (unsigned)data->display_id);
    return surface;
}

static void macos_destroy_surface(struct compositor_surface *surface) {
    if (!surface) return;
    macos_surface_data_t *data = surface->backend_data;
    if (data) {
        NSWindow *win = data->window;
        run_on_main(^{
            /* Detach the occlusion observer registered in attach_surface. */
            id obs = objc_getAssociatedObject(win, kNWOcclusionKey);
            if (obs) {
                [[NSNotificationCenter defaultCenter] removeObserver:obs];
                objc_setAssociatedObject(win, kNWOcclusionKey, nil,
                                         OBJC_ASSOCIATION_RETAIN);
            }
            [win close];
        });
        data->window = nil;
        data->view = nil;
        data->screen = nil;
        free(data);
    }
    free(surface);
}

static bool macos_configure_surface(struct compositor_surface *surface,
                                    const compositor_surface_config_t *config) {
    if (!surface || !config) return false;
    surface->config = *config;
    return true;
}

static void macos_commit_surface(struct compositor_surface *surface) {
    (void)surface; /* AppKit has no explicit commit. */
}

static bool macos_create_egl_window(struct compositor_surface *surface,
                                    int32_t width, int32_t height) {
    if (!surface) return false;
    surface->width = width;
    surface->height = height;
    /* The NSView IS the native window; nothing extra to allocate. */
    surface->egl_window = surface->native_surface;
    return true;
}

static bool macos_resize_egl_window(struct compositor_surface *surface,
                                    int32_t width, int32_t height) {
    if (!surface) return false;
    surface->width = width;
    surface->height = height;
    if (surface->egl_surface != EGL_NO_SURFACE) {
        egl_cgl_surface_resized(surface->egl_surface);
    }
    return true;
}

static EGLNativeWindowType
macos_get_native_window(struct compositor_surface *surface) {
    return surface ? (EGLNativeWindowType)surface->native_surface : NULL;
}

static void macos_destroy_egl_window(struct compositor_surface *surface) {
    if (surface) surface->egl_window = NULL;
}

static compositor_capabilities_t macos_get_capabilities(void *backend_data) {
    (void)backend_data;
    return COMPOSITOR_CAP_MULTI_OUTPUT | COMPOSITOR_CAP_OCCLUSION;
}

static void macos_damage_surface(struct compositor_surface *surface,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height) {
    (void)surface; (void)x; (void)y; (void)width; (void)height;
}

static void macos_set_scale(struct compositor_surface *surface, int32_t scale) {
    if (surface) surface->scale = scale;
}

/* ============================================================================
 * Output enumeration
 * ========================================================================== */

static struct output_state *macos_make_output(struct neowall_state *state,
                                              NSScreen *screen, int index) {
    struct output_state *out = calloc(1, sizeof(*out));
    if (!out) return NULL;

    NSRect frame = [screen frame];
    NSRect backing = [screen convertRectToBacking:frame];
    int scale = (int)[screen backingScaleFactor];
    if (scale < 1) scale = 1;

    out->state = state;
    out->name = (uint32_t)index;

    NSString *name = nil;
    if (@available(macOS 10.15, *)) {
        name = [screen localizedName];
    }
    const char *cname = name ? [name UTF8String] : "Display";
    snprintf(out->model, sizeof(out->model), "%.63s", cname);
    snprintf(out->connector_name, sizeof(out->connector_name), "Screen-%d", index);

    out->x_offset = (int32_t)frame.origin.x;
    out->y_offset = (int32_t)frame.origin.y;
    out->pixel_width = (int32_t)backing.size.width;
    out->pixel_height = (int32_t)backing.size.height;
    out->width = out->pixel_width;
    out->height = out->pixel_height;
    out->logical_width = (int32_t)frame.size.width;
    out->logical_height = (int32_t)frame.size.height;
    out->scale = scale;
    out->configured = true;
    atomic_init(&out->refcount, 1);

    out->config = calloc(1, sizeof(struct wallpaper_config));
    if (!out->config) {
        free(out);
        return NULL;
    }
    out->config->mode = MODE_FILL;
    out->config->transition = TRANSITION_NONE;
    out->config->transition_duration = 300;
    out->config->type = WALLPAPER_IMAGE;
    out->frame_timer_fd = -1;

    return out;
}

static bool macos_attach_surface(struct neowall_state *state,
                                 struct output_state *out, NSScreen *screen) {
    compositor_surface_config_t cfg = {
        .output = (__bridge void *)screen,
        .x = out->x_offset,
        .y = out->y_offset,
        .width = out->width,
        .height = out->height,
        .layer = COMPOSITOR_LAYER_BACKGROUND,
        .anchor = COMPOSITOR_ANCHOR_FILL,
        .exclusive_zone = 0,
        .keyboard_interactivity = false,
    };
    out->compositor_surface =
        compositor_surface_create(state->compositor_backend, &cfg);
    if (!out->compositor_surface) return false;

    out->width = out->compositor_surface->width;
    out->height = out->compositor_surface->height;
    out->pixel_width = out->compositor_surface->width;
    out->pixel_height = out->compositor_surface->height;

    /* Wire native occlusion -> engine pause. */
    macos_surface_data_t *sdata = out->compositor_surface->backend_data;
    if (sdata && sdata->window) {
        NWOcclusionObserver *obs = [NWOcclusionObserver new];
        obs.output = out;
        objc_setAssociatedObject(sdata->window, kNWOcclusionKey, obs,
                                 OBJC_ASSOCIATION_RETAIN);
        [[NSNotificationCenter defaultCenter]
            addObserver:obs
               selector:@selector(occlusionChanged:)
                   name:NSWindowDidChangeOcclusionStateNotification
                 object:sdata->window];
    }
    return true;
}

static bool macos_init_outputs(void *backend_data, struct neowall_state *state) {
    macos_backend_data_t *backend = backend_data;
    if (!backend || !state) return false;

    __block NSArray<NSScreen *> *screens = nil;
    run_on_main(^{ screens = [NSScreen screens]; });

    log_info("macOS: detected %lu screen(s)", (unsigned long)[screens count]);

    int created = 0;
    int index = 0;
    for (NSScreen *screen in screens) {
        struct output_state *out = macos_make_output(state, screen, index++);
        if (!out) continue;

        pthread_rwlock_wrlock(&state->output_list_lock);
        out->next = state->outputs;
        state->outputs = out;
        state->output_count++;
        pthread_rwlock_unlock(&state->output_list_lock);

        if (!macos_attach_surface(state, out, screen)) {
            log_error("macOS: failed to create desktop window for screen %d",
                      index - 1);
            pthread_rwlock_wrlock(&state->output_list_lock);
            struct output_state **pp = &state->outputs;
            while (*pp && *pp != out) pp = &(*pp)->next;
            if (*pp == out) {
                *pp = out->next;
                if (state->output_count > 0) state->output_count--;
            }
            pthread_rwlock_unlock(&state->output_list_lock);
            output_unref(out);
            continue;
        }

        log_info("macOS output ready: %s (%dx%d @%dx)",
                 out->model, out->width, out->height, out->scale);
        created++;
    }
    return created > 0;
}

/* ============================================================================
 * Backend lifecycle + event ops
 * ========================================================================== */

static void *macos_init(struct neowall_state *state) {
    macos_backend_data_t *backend = calloc(1, sizeof(*backend));
    if (!backend) return NULL;
    backend->state = state;

    int p[2];
    if (pipe(p) < 0) {
        free(backend);
        return NULL;
    }
    for (int i = 0; i < 2; i++) {
        fcntl(p[i], F_SETFL, O_NONBLOCK);
        fcntl(p[i], F_SETFD, FD_CLOEXEC);
    }
    backend->pipe_read = p[0];
    backend->pipe_write = p[1];
    backend->initialized = true;

    g_backend = backend;
    CGDisplayRegisterReconfigurationCallback(display_reconfig_cb, NULL);

    log_info("macOS AppKit backend initialized (desktop-level windows)");
    return backend;
}

static void macos_cleanup(void *backend_data) {
    macos_backend_data_t *backend = backend_data;
    if (!backend) return;
    CGDisplayRemoveReconfigurationCallback(display_reconfig_cb, NULL);
    g_backend = NULL;
    if (backend->pipe_read >= 0) close(backend->pipe_read);
    if (backend->pipe_write >= 0) close(backend->pipe_write);
    free(backend);
}

static int macos_get_fd(void *backend_data) {
    macos_backend_data_t *backend = backend_data;
    return backend ? backend->pipe_read : -1;
}

static bool macos_prepare_events(void *backend_data) {
    (void)backend_data;
    return true;
}

static bool macos_read_events(void *backend_data) {
    macos_backend_data_t *backend = backend_data;
    if (!backend) return false;
    uint8_t buf[16];
    while (read(backend->pipe_read, buf, sizeof(buf)) > 0) { /* drain */ }
    /* Display topology changed. For now log it; full hotplug reconcile can
     * reuse the X11 pattern in a follow-up. */
    log_info("macOS: display configuration changed");
    return true;
}

static bool macos_dispatch_events(void *backend_data) {
    (void)backend_data;
    return true;
}

static bool macos_flush(void *backend_data) {
    (void)backend_data;
    return true;
}

static void macos_cancel_read(void *backend_data) {
    (void)backend_data;
}

static int macos_get_error(void *backend_data) {
    (void)backend_data;
    return 0;
}

static bool macos_sync(void *backend_data) {
    (void)backend_data;
    return true;
}

static void *macos_get_native_display(void *backend_data) {
    (void)backend_data;
    return NULL; /* CGL needs no native display; egl shim ignores it. */
}

static EGLenum macos_get_egl_platform(void *backend_data) {
    (void)backend_data;
    return 0;
}

static const compositor_backend_ops_t macos_ops = {
    .init = macos_init,
    .cleanup = macos_cleanup,
    .create_surface = macos_create_surface,
    .destroy_surface = macos_destroy_surface,
    .configure_surface = macos_configure_surface,
    .commit_surface = macos_commit_surface,
    .create_egl_window = macos_create_egl_window,
    .resize_egl_window = macos_resize_egl_window,
    .get_native_window = macos_get_native_window,
    .destroy_egl_window = macos_destroy_egl_window,
    .get_capabilities = macos_get_capabilities,
    .damage_surface = macos_damage_surface,
    .set_scale = macos_set_scale,
    .init_outputs = macos_init_outputs,
    .get_fd = macos_get_fd,
    .prepare_events = macos_prepare_events,
    .read_events = macos_read_events,
    .dispatch_events = macos_dispatch_events,
    .flush = macos_flush,
    .cancel_read = macos_cancel_read,
    .get_error = macos_get_error,
    .sync = macos_sync,
    .get_native_display = macos_get_native_display,
    .get_egl_platform = macos_get_egl_platform,
};

/* Entry point mirroring compositor_backend_x11_init(). */
struct compositor_backend *compositor_backend_macos_init(struct neowall_state *state) {
    void *data = macos_init(state);
    if (!data) return NULL;

    struct compositor_backend *backend = calloc(1, sizeof(*backend));
    if (!backend) {
        macos_cleanup(data);
        return NULL;
    }
    backend->name = "macos-appkit";
    backend->description = "macOS AppKit desktop-level window backend";
    backend->priority = 100;
    backend->ops = &macos_ops;
    backend->data = data;
    backend->capabilities = macos_get_capabilities(data);
    return backend;
}

#endif /* __APPLE__ */
