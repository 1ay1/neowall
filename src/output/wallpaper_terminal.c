/* Show a live terminal as the wallpaper. */

#include "neowall/surface/surface.h"
#include "neowall/egl/egl_core.h"
#include "render.h"
#include "output_internal.h"
#include "neowall/core/log.h"
#include "neowall/core/time.h"
#include "neowall/core/util.h"
#include "neowall/state/state.h"

/* Built-in enhanced terminal pass-through: sample the whole grid across the
 * frame with the nwTermFX post effects (bloom/scanline/CRT). The effect
 * intensities are fed via iTermFX from the config; all-zero => crisp nwTerm.
 * Used when the terminal wallpaper names no styling shader. */
static const char *kTermCrispShader = "void mainImage(out vec4 fragColor, in vec2 fragCoord){\n"
                                      "    vec2 uv = fragCoord / iResolution.xy;\n"
                                      "    fragColor = vec4(nwTermFX(uv), 1.0);\n"
                                      "}\n";

/* The terminal reader thread calls this when the child wrote something. */
static void terminal_output_woke(void *ctx) {
    struct output_state *o = ctx;
    state_output_dirty(o->state, o);
}

nw_result output_set_terminal(struct output_state *output, const char *cmd, const char *shader_path,
                              const char *font_path, int cols, int rows) {
    if (!output || !cmd || !cmd[0]) {
        return nw_err(NW_ERR_INVALID_ARG, "output_set_terminal: null cmd");
    }
    if (!output->state || output->state->egl_display == EGL_NO_DISPLAY) {
        return nw_err(NW_ERR_GL, "EGL display not initialized");
    }
    if (!output->compositor_surface || output->compositor_surface->egl_surface == EGL_NO_SURFACE) {
        /* Surface not up yet: stash it on the config and let the render loop
         * re-apply once the surface arrives (mirrors output_set_shader). */
        pthread_mutex_lock(&output->state->state_mutex);
        output->config->type = WALLPAPER_TERMINAL;
        config_str_set(output->config->term_cmd, sizeof(output->config->term_cmd), cmd);
        if (shader_path)
            config_str_set(output->config->shader_path, sizeof(output->config->shader_path),
                           shader_path);
        if (font_path)
            config_str_set(output->config->term_font, sizeof(output->config->term_font), font_path);
        output->config->term_cols = cols;
        output->config->term_rows = rows;
        pthread_mutex_unlock(&output->state->state_mutex);
        return nw_ok();
    }

    if (!egl_make_surface_current(output->state->egl_display,
                                  output->compositor_surface->egl_surface,
                                  output->state->egl_context)) {
        return nw_err(NW_ERR_GL, "eglMakeCurrent failed for terminal set");
    }

    /* Idempotency guard against a double-spawn.  Several paths can drive this
     * function for the SAME output+command — output_apply_config on the
     * surface-ready event, output_apply_deferred_config, and a config reload —
     * and if two of them run before the first has published its shader, or a
     * reload re-applies an unchanged terminal block, we would spawn a second
     * child. Bail out when a terminal wallpaper with this exact command is
     * already live and its child is still running: nothing to do. A DIFFERENT
     * command still swaps normally (the command-change path below), and a
     * dead child still respawns (that is the intended "restart on exit"). */
    if (output->config->type == WALLPAPER_TERMINAL && output->shader != NULL &&
        !term_gpu_child_exited(wallshader_terminal(output->shader)) &&
        strcmp(output->config->term_cmd, cmd) == 0) {
        log_debug("Terminal wallpaper '%s' already running on output %s; "
                  "skipping redundant respawn",
                  cmd, output->model[0] ? output->model : "unknown");
        return nw_ok();
    }

    /* Build the replacement off to the side.  In particular, do not tear down
     * the current terminal here: freeing it also kills its PTY child, so
     * every load/attach/compile failure must leave it running. */
    wallshader *candidate = NULL;

    /* Styling shader (optional) or the built-in crisp pass-through. */
    nw_result lr = nw_err(NW_ERR_NOT_FOUND, "no styling shader");
    if (shader_path && shader_path[0]) {
        lr = wallshader_load(shader_path, &candidate);
        if (nw_is_err(lr)) {
            log_warn("Terminal styling shader '%s' didn't load; using crisp pass-through",
                     shader_path);
        }
    }
    if (nw_is_err(lr)) lr = wallshader_load_text(kTermCrispShader, NULL, &candidate);
    if (nw_is_err(lr)) {
        return lr;
    }

    /* Derive a grid from the output size if not specified. Cell size is fixed at
     * a legible 9x18 by default; term_font_size overrides the cell HEIGHT (width
     * scales ~0.5x to keep a monospace aspect). */
    int cell_h = output->config->term_font_size > 0 ? output->config->term_font_size : 18;
    if (cell_h < 6) cell_h = 6;
    if (cell_h > 96) cell_h = 96;
    int cell_w = (cell_h + 1) / 2;
    if (cell_w < 3) cell_w = 3;
    bool auto_cols = cols <= 0;
    bool auto_rows = rows <= 0;
    int gc         = cols > 0 ? cols : output->width / cell_w;
    int gr         = rows > 0 ? rows : output->height / cell_h;
    if (gc < 1) gc = 80;
    if (gr < 1) gr = 24;

    /* A settled terminal stops redrawing and lets the loop sleep. Its child's
     * output is what wakes it: the reader thread posts a redraw for this
     * output. */
    term_gpu_opts to = {
        .cmd       = cmd,
        .cols      = gc,
        .rows      = gr,
        .cell_w    = cell_w,
        .cell_h    = cell_h,
        .font_path = (font_path && font_path[0]) ? font_path : NULL,
        .font_bold = output->config->term_font_bold[0] ? output->config->term_font_bold : NULL,
        .font_italic =
            output->config->term_font_italic[0] ? output->config->term_font_italic : NULL,
        .cwd           = output->config->term_cwd,
        .term_env      = output->config->term_env,
        .fg            = output->config->term_fg >= 0 ? output->config->term_fg : -1,
        .bg            = output->config->term_bg >= 0 ? output->config->term_bg : -1,
        .on_output     = terminal_output_woke,
        .on_output_ctx = output,
    };
    term_gpu *term = NULL;
    nw_result tr   = term_gpu_new(&to, &term);
    if (nw_is_err(tr)) {
        wallshader_free(candidate);
        return tr;
    }

    /* nwTermFX intensities. A -1 config sentinel means "use the built-in
     * default": a subtle bloom so bright/bold cells glow, and the retro CRT
     * bits (scanline/curve/chroma) off unless the user opts in. The
     * change-driven fade defaults on: graphs and numbers glide. */
    const float fx[4] = {
        output->config->term_bloom >= 0.0f ? output->config->term_bloom : 0.35f,
        output->config->term_scanline >= 0.0f ? output->config->term_scanline : 0.0f,
        output->config->term_crt >= 0.0f ? output->config->term_crt : 0.0f,
        output->config->term_chroma >= 0.0f ? output->config->term_chroma : 0.0f,
    };
    term_gpu_set_fx(term, fx, output->config->term_fade >= 0.0f ? output->config->term_fade : 0.5f);
    wallshader_attach_terminal(candidate, term);

    /* Candidate is now complete.  Publish it, then retire the old terminal
     * child and GL resources. */
    wallshader *old = output->shader;
    output->shader  = candidate;
    wallshader_free(old);

    output->shader_start_time = get_time_ms();
    output->frames_rendered   = 0;
    output->shader_paused_at  = 0;

    pthread_mutex_lock(&output->state->state_mutex);
    output->config->type = WALLPAPER_TERMINAL;
    config_str_set(output->config->term_cmd, sizeof(output->config->term_cmd), cmd);
    if (shader_path)
        config_str_set(output->config->shader_path, sizeof(output->config->shader_path),
                       shader_path);
    else
        output->config->shader_path[0] = '\0';
    output->config->term_cols  = gc;
    output->config->term_rows  = gr;
    output->terminal_auto_cols = auto_cols;
    output->terminal_auto_rows = auto_rows;
    pthread_mutex_unlock(&output->state->state_mutex);

    /* The surface was created before the config resolved as a terminal, so it
     * is keyboard-inert until this point. Make it typeable now. */
    output_sync_keyboard_focus(output);

    /* Terminal wallpapers don't use images; free any lingering texture. */
    if (output->current_image) {
        image_free(output->current_image);
        output->current_image = NULL;
    }
    if (output->texture) {
        render_destroy_texture(output->texture);
        output->texture = 0;
    }

    atomic_store_explicit(&output->needs_redraw, true, memory_order_release);
    uint64_t now            = get_time_ms();
    output->last_frame_time = now;
    output->last_cycle_time = now;
    output_configure_vsync(output);
    output_configure_frame_timer(output);

    log_info("Terminal wallpaper running: '%s' (%dx%d cells)", cmd, gc, gr);
    return nw_ok();
}
