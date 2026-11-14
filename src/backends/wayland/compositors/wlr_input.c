/* wlr-layer-shell backend: pointer, keyboard and seat input */

#include "wlr_internal.h"

/* ============================================================================
 * POINTER EVENT HANDLERS
 * ============================================================================ */

void wlr_set_ptr_output(wlr_backend_data_t *backend, struct output_state *output) {
    if (!backend || backend->ptr_output == output) return;
    if (output) output_ref(output);
    struct output_state *old = backend->ptr_output;
    backend->ptr_output      = output;
    if (old) output_unref(old);
}

void wlr_set_kbd_output(wlr_backend_data_t *backend, struct output_state *output) {
    if (!backend || backend->kbd_output == output) return;
    if (output) output_ref(output);
    struct output_state *old = backend->kbd_output;
    backend->kbd_output      = output;
    if (old) output_unref(old);
}

void wlr_clear_input_focus(wlr_backend_data_t *backend) {
    if (!backend) return;
    backend->ptr_buttons = 0;
    wlr_set_ptr_output(backend, NULL);
    wlr_set_kbd_output(backend, NULL);
}

/* Wayland pointer coordinates are surface-logical. Rendering and terminal
 * hit-testing use buffer pixels, including fractional-scale buffers. */
static void pointer_to_buffer(const struct output_state *output, double logical_x, double logical_y,
                              int *buffer_x, int *buffer_y) {
    int logical_w = output && output->logical_width > 0 ? output->logical_width : 0;
    int logical_h = output && output->logical_height > 0 ? output->logical_height : 0;
    if (output && output->xdg_logical_width > 0) logical_w = output->xdg_logical_width;
    if (output && output->xdg_logical_height > 0) logical_h = output->xdg_logical_height;
    *buffer_x = output && logical_w > 0 && output->width > 0
                    ? (int)(logical_x * output->width / logical_w)
                    : (int)logical_x;
    *buffer_y = output && logical_h > 0 && output->height > 0
                    ? (int)(logical_y * output->height / logical_h)
                    : (int)logical_y;
}

/* Destroy the wl_pointer together with its cursor-shape device. The shape
 * device wraps this specific pointer object; keeping it across a pointer
 * rebind would reference a dead proxy. */
void wlr_destroy_pointer(wlr_backend_data_t *backend) {
    wayland_t *wl = wayland_get();
    if (wl && wl->cursor_shape_device) {
        wp_cursor_shape_device_v1_destroy(wl->cursor_shape_device);
        wl->cursor_shape_device = NULL;
    }
    if (backend->pointer) {
        wl_pointer_destroy(backend->pointer);
        backend->pointer = NULL;
    }
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t surface_x,
                                 wl_fixed_t surface_y) {
    wlr_backend_data_t *backend = data;

    double x = wl_fixed_to_double(surface_x);
    double y = wl_fixed_to_double(surface_y);

    log_debug("Wayland pointer entered surface at (%.2f, %.2f)", x, y);

    /* Record which output the pointer entered + its surface-local position, so
     * motion/button/axis can forward mouse reports to a terminal wallpaper. */
    if (backend && backend->state && surface) {
        struct output_state *entered = NULL;
        pthread_rwlock_rdlock(&backend->state->output_list_lock);
        for (struct output_state *o = backend->state->outputs; o; o = o->next) {
            if (o->compositor_surface &&
                (struct wl_surface *)o->compositor_surface->native_surface == surface) {
                entered = o;
                output_ref(entered);
                break;
            }
        }
        pthread_rwlock_unlock(&backend->state->output_list_lock);
        wlr_set_ptr_output(backend, entered);
        if (entered) {
            pointer_to_buffer(entered, x, y, &backend->ptr_x, &backend->ptr_y);
            output_unref(entered);
        }
    }

    /* Set the cursor when pointer enters the wallpaper surface.
     *
     * We honor the user's XCURSOR_THEME / XCURSOR_SIZE (read at init time)
     * and load the theme at the entered output's buffer scale so it stays
     * crisp on HiDPI monitors. Without this, the cursor falls back to a
     * blurry 24px wlroots default (see GitHub issue: Hyprland + Bibata
     * losing the user's theme over the wallpaper).
     */
    wayland_t *wl = wayland_get();
    if (!wl) {
        return;
    }

    /* Preferred path: wp_cursor_shape_manager_v1. We just name the shape
     * ("default") and the COMPOSITOR draws its own cursor — same theme,
     * size and fractional scaling as over every other window (Hyprland
     * renders hyprcursor themes server-side; an XCursor buffer we attach
     * ourselves can never match that). */
    if (wl->cursor_shape_manager) {
        if (!wl->cursor_shape_device) {
            wl->cursor_shape_device =
                wp_cursor_shape_manager_v1_get_pointer(wl->cursor_shape_manager, pointer);
        }
        if (wl->cursor_shape_device) {
            wp_cursor_shape_device_v1_set_shape(wl->cursor_shape_device, serial,
                                                WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
            return;
        }
    }

    /* Fallback: client-side XCursor buffer honoring XCURSOR_THEME/SIZE. */
    if (!wl->cursor_surface || !wl->shm) {
        return;
    }

    /* Find the output whose surface the pointer entered, to get its scale. */
    int32_t scale = 1;
    if (backend && backend->state && surface) {
        pthread_rwlock_rdlock(&backend->state->output_list_lock);
        for (struct output_state *o = backend->state->outputs; o; o = o->next) {
            if (o->compositor_surface &&
                (struct wl_surface *)o->compositor_surface->native_surface == surface) {
                if (o->scale > 0) {
                    scale = o->scale;
                }
                break;
            }
        }
        pthread_rwlock_unlock(&backend->state->output_list_lock);
    }

    /* (Re)load the theme if needed: first time, or scale changed. */
    if (!wl->cursor_theme || wl->cursor_loaded_scale != scale) {
        if (wl->cursor_theme) {
            wl_cursor_theme_destroy(wl->cursor_theme);
            wl->cursor_theme = NULL;
        }
        int size         = wl->cursor_base_size * scale;
        wl->cursor_theme = wl_cursor_theme_load(wl->cursor_theme_name, size, wl->shm);
        if (!wl->cursor_theme) {
            log_warn("Failed to load cursor theme '%s' at size %d",
                     wl->cursor_theme_name ? wl->cursor_theme_name : "<default>", size);
            return;
        }
        wl->cursor_loaded_scale = scale;
        log_debug("Loaded cursor theme '%s' at size %d (scale %d)",
                  wl->cursor_theme_name ? wl->cursor_theme_name : "<default>", size, scale);
    }

    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(wl->cursor_theme, "left_ptr");
    if (!cursor || cursor->image_count == 0) {
        /* Fall back to "default" alias used by some themes (e.g. Adwaita). */
        cursor = wl_cursor_theme_get_cursor(wl->cursor_theme, "default");
    }
    if (!cursor || cursor->image_count == 0) {
        return;
    }

    struct wl_cursor_image *image = cursor->images[0];
    struct wl_buffer *buffer      = wl_cursor_image_get_buffer(image);
    if (!buffer) {
        return;
    }

    /* Tell the compositor the cursor buffer is at `scale` so it's displayed
     * at the correct logical size and not blurry on HiDPI. */
    wl_surface_set_buffer_scale(wl->cursor_surface, scale);
    wl_surface_attach(wl->cursor_surface, buffer, 0, 0);
    wl_surface_damage_buffer(wl->cursor_surface, 0, 0, (int32_t)image->width,
                             (int32_t)image->height);
    wl_surface_commit(wl->cursor_surface);
    wl_pointer_set_cursor(pointer, serial, wl->cursor_surface, (int32_t)image->hotspot_x / scale,
                          (int32_t)image->hotspot_y / scale);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface) {
    wlr_backend_data_t *backend = data;
    (void)pointer;
    (void)serial;
    (void)surface;

    log_debug("Wayland pointer left surface");

    if (backend) {
        wlr_set_ptr_output(backend, NULL);
        backend->ptr_buttons = 0;
    }
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t surface_x, wl_fixed_t surface_y) {
    wlr_backend_data_t *backend = data;
    (void)pointer;
    (void)time;

    double x = wl_fixed_to_double(surface_x);
    double y = wl_fixed_to_double(surface_y);

    /* Feed motion speed into the reactive subsystem (iMouseEnergy). */
    {
        static double prev_x = 0.0, prev_y = 0.0;
        static bool have_prev = false;
        if (have_prev) {
            reactive_note_mouse((float)(x - prev_x), (float)(y - prev_y));
        }
        prev_x    = x;
        prev_y    = y;
        have_prev = true;
    }

    /* Throttle logging to avoid spam */
    static uint64_t last_motion_log = 0;
    uint64_t now                    = get_time_ms();
    if (now - last_motion_log > 2000) {
        log_debug("Wayland pointer motion: (%.2f, %.2f)", x, y);
        last_motion_log = now;
    }

    /* Surface-local coordinates belong only to the entered output. Convert
     * them to that output's render-buffer space (important on HiDPI) rather
     * than copying one output's local coordinates to every monitor. */
    if (backend && backend->ptr_output) {
        pointer_to_buffer(backend->ptr_output, x, y, &backend->ptr_x, &backend->ptr_y);
        backend->ptr_output->mouse_x = (float)backend->ptr_x;
        backend->ptr_output->mouse_y = (float)backend->ptr_y;

        /* The current terminal API cannot query 1002 vs 1003, so emit motion
         * only for a real drag. This keeps 1002 correct and tracks all three
         * standard buttons instead of pretending every drag is left-button. */
        if (backend->ptr_buttons) {
            int drag_button = (backend->ptr_buttons & 1u) ? 0 : (backend->ptr_buttons & 2u) ? 1 : 2;
            output_terminal_mouse(backend->ptr_output, backend->ptr_x, backend->ptr_y, drag_button,
                                  true, /*motion=*/true);
        }
    }
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                                  uint32_t time, uint32_t button, uint32_t state) {
    wlr_backend_data_t *backend = data;
    (void)pointer;
    (void)serial;
    (void)time;

    const char *state_str = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? "pressed" : "released";
    log_debug("Wayland pointer button %s: button %u", state_str, button);

    /* Map Linux evdev button codes to terminal mouse buttons. */
    bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    int tbtn;
    switch (button) {
    case 0x110: tbtn = 0; break; /* BTN_LEFT   */
    case 0x112: tbtn = 1; break; /* BTN_MIDDLE */
    case 0x111: tbtn = 2; break; /* BTN_RIGHT  */
    default: tbtn = 0; break;
    }
    if (backend && tbtn >= 0 && tbtn < 3) {
        uint8_t mask = (uint8_t)(1u << tbtn);
        if (pressed)
            backend->ptr_buttons |= mask;
        else
            backend->ptr_buttons &= (uint8_t)~mask;
    }

    /* Always preserve the physical button. term_mouse() selects legacy's
     * generic release code itself, while SGR requires the actual button with
     * an 'm' final. */
    if (backend && backend->ptr_output) {
        output_terminal_mouse(backend->ptr_output, backend->ptr_x, backend->ptr_y, tbtn, pressed,
                              /*motion=*/false);
    }
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value) {
    wlr_backend_data_t *backend = data;
    (void)pointer;
    (void)time;

    const char *axis_name = (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) ? "vertical" : "horizontal";
    double val            = wl_fixed_to_double(value);
    log_debug("Wayland pointer axis %s: %.2f", axis_name, val);

    /* Forward vertical scroll to the terminal as wheel buttons 64 (up) / 65
     * (down). A press-only report (no release) is the xterm convention. */
    if (backend && backend->ptr_output && axis == WL_POINTER_AXIS_VERTICAL_SCROLL && val != 0.0) {
        int wbtn = val < 0.0 ? 64 : 65;
        output_terminal_mouse(backend->ptr_output, backend->ptr_x, backend->ptr_y, wbtn, true,
                              /*motion=*/false);
    }
}

static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
    (void)data;
    (void)pointer;
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                                       uint32_t axis_source) {
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                                     uint32_t axis) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                         int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

const struct wl_pointer_listener wlr_pointer_listener = {
    .enter         = pointer_handle_enter,
    .leave         = pointer_handle_leave,
    .motion        = pointer_handle_motion,
    .button        = pointer_handle_button,
    .axis          = pointer_handle_axis,
    .frame         = pointer_handle_frame,
    .axis_source   = pointer_handle_axis_source,
    .axis_stop     = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

/* ============================================================================
 * KEYBOARD EVENT HANDLERS (terminal-wallpaper typing)
 * ============================================================================ */

#ifdef NEOWALL_HAVE_XKB
/* Find the output_state whose compositor surface matches a wl_surface. */
static struct output_state *output_for_surface(wlr_backend_data_t *b, struct wl_surface *surf) {
    if (!b || !b->state || !surf) return NULL;
    for (struct output_state *o = b->state->outputs; o; o = o->next) {
        if (o->compositor_surface && o->compositor_surface->native_surface == surf) return o;
    }
    return NULL;
}

/* Translate an xkb keysym (+ current modifiers) into the VT byte sequence a
 * terminal expects, writing it to `out` (cap bytes). Returns the byte count, or
 * 0 if the key produces nothing. Covers control chars, the cursor/edit/function
 * keys, and Alt-as-ESC-prefix; ordinary text comes from xkb_state_key_get_utf8. */
static int keysym_to_vt(struct xkb_state *st, xkb_keysym_t sym, uint32_t keycode, bool ctrl,
                        bool alt, char *out, int cap) {
    int n = 0;
#define EMIT(s)                                                                                    \
    do {                                                                                           \
        const char *_s = (s);                                                                      \
        while (*_s && n < cap)                                                                     \
            out[n++] = *_s++;                                                                      \
    } while (0)
    /* Alt sends ESC prefix before the key's normal bytes. */
    if (alt && n < cap) out[n++] = 0x1b;

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: EMIT("\r"); return n;
    case XKB_KEY_BackSpace: EMIT("\x7f"); return n;
    case XKB_KEY_Tab: EMIT("\t"); return n;
    case XKB_KEY_Escape: EMIT("\x1b"); return n;
    case XKB_KEY_Up: EMIT("\x1b[A"); return n;
    case XKB_KEY_Down: EMIT("\x1b[B"); return n;
    case XKB_KEY_Right: EMIT("\x1b[C"); return n;
    case XKB_KEY_Left: EMIT("\x1b[D"); return n;
    case XKB_KEY_Home: EMIT("\x1b[H"); return n;
    case XKB_KEY_End: EMIT("\x1b[F"); return n;
    case XKB_KEY_Insert: EMIT("\x1b[2~"); return n;
    case XKB_KEY_Delete: EMIT("\x1b[3~"); return n;
    case XKB_KEY_Page_Up: EMIT("\x1b[5~"); return n;
    case XKB_KEY_Page_Down: EMIT("\x1b[6~"); return n;
    case XKB_KEY_F1: EMIT("\x1bOP"); return n;
    case XKB_KEY_F2: EMIT("\x1bOQ"); return n;
    case XKB_KEY_F3: EMIT("\x1bOR"); return n;
    case XKB_KEY_F4: EMIT("\x1bOS"); return n;
    case XKB_KEY_F5: EMIT("\x1b[15~"); return n;
    case XKB_KEY_F6: EMIT("\x1b[17~"); return n;
    case XKB_KEY_F7: EMIT("\x1b[18~"); return n;
    case XKB_KEY_F8: EMIT("\x1b[19~"); return n;
    case XKB_KEY_F9: EMIT("\x1b[20~"); return n;
    case XKB_KEY_F10: EMIT("\x1b[21~"); return n;
    case XKB_KEY_F11: EMIT("\x1b[23~"); return n;
    case XKB_KEY_F12: EMIT("\x1b[24~"); return n;
    default: break;
    }

    /* Ctrl+letter -> control code (Ctrl-A = 0x01 .. Ctrl-Z = 0x1a), plus the
     * classic Ctrl-[\]^_ and Ctrl-Space=NUL. */
    if (ctrl) {
        xkb_keysym_t low = sym;
        if (low >= XKB_KEY_A && low <= XKB_KEY_Z) low += 32;
        if (low >= XKB_KEY_a && low <= XKB_KEY_z) {
            if (n < cap) out[n++] = (char)(low - 'a' + 1);
            return n;
        }
        if (low == XKB_KEY_space || low == XKB_KEY_2) {
            if (n < cap) out[n++] = 0;
            return n;
        }
        if (low >= XKB_KEY_bracketleft && low <= XKB_KEY_underscore) {
            if (n < cap) out[n++] = (char)(low - XKB_KEY_bracketleft + 0x1b);
            return n;
        }
    }

    /* Ordinary text: let xkb produce the UTF-8 for the keycode with the layout
     * + shift/level already applied. */
    char buf[16];
    int len = xkb_state_key_get_utf8(st, keycode, buf, sizeof(buf));
    for (int i = 0; i < len && n < cap; i++)
        out[n++] = buf[i];
    return n;
#undef EMIT
}

static void kb_handle_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int32_t fd,
                             uint32_t size) {
    wlr_backend_data_t *b = data;
    (void)kb;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }
    if (!b->xkb_ctx) b->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *km =
        b->xkb_ctx ? xkb_keymap_new_from_string(b->xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS)
                   : NULL;
    munmap(map, size);
    close(fd);
    if (!km) return;
    if (b->xkb_state) xkb_state_unref(b->xkb_state);
    if (b->xkb_keymap) xkb_keymap_unref(b->xkb_keymap);
    b->xkb_keymap = km;
    b->xkb_state  = xkb_state_new(km);
}

static void kb_handle_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
                            struct wl_surface *surface, struct wl_array *keys) {
    wlr_backend_data_t *b = data;
    (void)kb;
    (void)serial;
    (void)keys;
    wlr_set_kbd_output(b, output_for_surface(b, surface));
    if (b->kbd_output) log_debug("Keyboard focus entered a terminal wallpaper");
}

static void kb_handle_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
                            struct wl_surface *surface) {
    wlr_backend_data_t *b = data;
    (void)kb;
    (void)serial;
    (void)surface;
    wlr_set_kbd_output(b, NULL);
}

static void kb_handle_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
                          uint32_t key, uint32_t state) {
    wlr_backend_data_t *b = data;
    (void)kb;
    (void)serial;
    (void)time;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (!b->kbd_output || !b->xkb_state) return;

    uint32_t keycode = key + 8; /* evdev -> xkb keycode offset */
    xkb_keysym_t sym = xkb_state_key_get_one_sym(b->xkb_state, keycode);
    bool ctrl =
        xkb_state_mod_name_is_active(b->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    bool alt =
        xkb_state_mod_name_is_active(b->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
    char seq[32];
    int n = keysym_to_vt(b->xkb_state, sym, keycode, ctrl, alt, seq, sizeof(seq));
    if (n <= 0) return;

    /* Guard the wallpaper against a stray control key tearing down the app.
     * Unless the user opted into raw input, drop the control bytes that make
     * the child's line discipline generate a signal or EOF:
     *   0x03 Ctrl-C  SIGINT      0x1c Ctrl-\  SIGQUIT
     *   0x04 Ctrl-D  EOF (exits shells/REPLs)
     *   0x1a Ctrl-Z  SIGTSTP (suspends — the wallpaper would freeze)
     * Everything else (text, arrows, function keys, Tab, Enter, other Ctrl
     * combos a TUI actually wants) still passes through untouched. */
    bool raw = b->state && atomic_load_explicit(&b->state->term_raw_input, memory_order_acquire);
    if (!raw && n == 1 && (seq[0] == 0x03 || seq[0] == 0x04 || seq[0] == 0x1a || seq[0] == 0x1c)) {
        return;
    }

    output_terminal_key(b->kbd_output, seq, (size_t)n);
}

static void kb_handle_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t dep,
                                uint32_t lat, uint32_t lck, uint32_t grp) {
    wlr_backend_data_t *b = data;
    (void)kb;
    (void)serial;
    if (b->xkb_state) xkb_state_update_mask(b->xkb_state, dep, lat, lck, 0, 0, grp);
}

static void kb_handle_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay) {
    (void)data;
    (void)kb;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap      = kb_handle_keymap,
    .enter       = kb_handle_enter,
    .leave       = kb_handle_leave,
    .key         = kb_handle_key,
    .modifiers   = kb_handle_modifiers,
    .repeat_info = kb_handle_repeat_info,
};
#endif /* NEOWALL_HAVE_XKB */

/* ============================================================================
 * SEAT EVENT HANDLERS
 * ============================================================================ */

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    wlr_backend_data_t *backend = data;

    log_debug("Wayland seat capabilities: 0x%x", capabilities);

    backend->has_pointer_cap = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        /* Respect the global mouse_interaction setting. If disabled, don't bind
         * wl_pointer at all — the compositor will then route pointer events to
         * whatever surface is underneath us (i.e. the normal desktop), and our
         * wallpaper stays invisible to the pointer the way the user asked. */
        bool want_pointer =
            backend->state
                ? atomic_load_explicit(&backend->state->mouse_interaction, memory_order_acquire)
                : true;
        if (want_pointer && !backend->pointer) {
            backend->pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(backend->pointer, &wlr_pointer_listener, backend);
            log_info("Wayland pointer capability enabled");
        } else if (!want_pointer && !backend->pointer) {
            log_info("Wayland pointer capability available but mouse_interaction=false, "
                     "not binding pointer");
        } else if (!want_pointer && backend->pointer) {
            /* Capability re-advertise after a config that disabled pointer:
             * drop our binding so events stop flowing. */
            wlr_destroy_pointer(backend);
            wlr_set_ptr_output(backend, NULL);
            log_info("Wayland pointer released (mouse_interaction=false)");
        }
    } else {
        if (backend->pointer) {
            wlr_destroy_pointer(backend);
            log_info("Wayland pointer capability disabled");
        }
    }

#ifdef NEOWALL_HAVE_XKB
    /* Keyboard: bound whenever the seat has one, so a focused terminal
     * wallpaper can be typed into. Focus is opt-in per surface (ON_DEMAND
     * keyboard-interactivity) and only granted when the user clicks it, so
     * binding here does not steal keys from normal windows. */
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!backend->keyboard) {
            backend->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(backend->keyboard, &keyboard_listener, backend);
            log_info("Wayland keyboard capability enabled (terminal typing)");
        }
    } else if (backend->keyboard) {
        wlr_set_kbd_output(backend, NULL);
        wl_keyboard_destroy(backend->keyboard);
        backend->keyboard = NULL;
    }
#endif
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    log_debug("Wayland seat name: %s", name);
}

const struct wl_seat_listener wlr_seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name         = seat_handle_name,
};
