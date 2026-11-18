/* configure: turn the config file into what every output shows.
 *
 * config.c only reads and validates; this is where its answers meet the live
 * outputs. The rule (last-known-good): the whole file is checked before any
 * output changes, so a mistake anywhere leaves everything as it was. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_internal.h"
#include "neowall/source/source.h"
#include "neowall/app/app.h"
#include "neowall/surface/surface.h"
#include "neowall/config/config.h"
#include "neowall/core/log.h"
#include "neowall/output/output.h"
#include "neowall/state/state.h"

static const char *output_name(const struct output_state *o) {
    const char *n = o->connector_name[0] ? o->connector_name : o->model;
    return n[0] ? n : "unknown";
}

/* Every output gets the built-in default (an image found in the usual
 * places, or nothing). The first-run fallback when the file is unusable. */
static bool apply_builtin(struct neowall_state *state) {
    log_info("Applying built-in default configuration");
    struct wallpaper_config def;
    config_builtin_default(&def);
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        struct wallpaper_config copy = def; /* output_apply_config deep-copies */
        output_apply_config(o, &copy);
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    config_free_wallpaper(&def);
    return true;
}

/* Daemon-wide settings, then let the backend react (mouse binding). */
static void apply_globals(struct neowall_state *state, const config_doc *doc) {
    config_globals g = config_doc_globals(doc);
    if (g.has_mouse_interaction)
        atomic_store_explicit(&state->mouse_interaction, g.mouse_interaction, memory_order_release);
    if (g.has_term_raw_input)
        atomic_store_explicit(&state->term_raw_input, g.term_raw_input, memory_order_release);
    /* exec("...") manifest uniforms run commands: only with an explicit opt-in
     * in the user's own config or on the command line, never from a shader */
    nw_source_exec_set_allowed(state->allow_exec_cli || g.allow_exec);
    const struct compositor_backend *b = state->compositor_backend;
    if (b && b->ops && b->ops->apply_input_config) b->ops->apply_input_config(b->data, state);
}

/* Each output gets exactly one config: its own block, or default. */
static bool apply_doc(struct neowall_state *state, const config_doc *doc, bool *applied_any) {
    bool failed  = false;
    *applied_any = false;
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        struct wallpaper_config cfg;
        if (!config_doc_for_output(doc, o->connector_name, o->model, &cfg)) {
            log_debug("No config block applies to output '%s'", output_name(o));
            continue;
        }
        if (output_apply_config(o, &cfg)) {
            *applied_any = true;
            log_info("Output %s: %s", output_name(o),
                     cfg.type == WALLPAPER_SHADER     ? cfg.shader_path
                     : cfg.type == WALLPAPER_TERMINAL ? cfg.term_cmd
                                                      : cfg.path);
        } else {
            log_error("Failed to apply config to output '%s'", output_name(o));
            failed = true;
        }
        config_free_wallpaper(&cfg);
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    return !failed;
}

/* First load (startup): a missing file gets the starter config, an unusable
 * one falls back to the built-in default so the screen isn't left empty. */
bool app_config_load(struct neowall_state *state, const char *path) {
    if (!state || !path) return false;
    log_info("Loading configuration from: %s", path);
    struct stat st;
    if (stat(path, &st) == -1) {
        log_info("Configuration file not found, creating default: %s", path);
        if (!config_create_default(path)) {
            log_error("Failed to create default configuration, using built-in defaults");
            return apply_builtin(state);
        }
    }
    config_doc *doc = config_doc_load(path);
    if (!doc) {
        log_error("Using built-in default configuration");
        return apply_builtin(state);
    }
    apply_globals(state, doc);
    bool any = false;
    bool ok  = apply_doc(state, doc, &any);
    config_doc_free(doc);
    if (!ok) {
        log_error("Configuration was valid, but one or more outputs failed to apply it");
        return false;
    }
    if (!any) return apply_builtin(state);
    log_info("Configuration loaded from %s", path);
    return true;
}

/* Reload: any problem leaves the live config exactly as it is. */
bool app_config_reload(struct neowall_state *state, const char *path) {
    if (!state || !path) return false;
    log_info("Loading configuration from: %s", path);
    struct stat st;
    if (stat(path, &st) == -1) {
        log_error("Reload could not stat configuration '%s': %s; live configuration unchanged",
                  path, strerror(errno));
        return false;
    }
    config_doc *doc = config_doc_load(path);
    if (!doc) {
        log_error("Configuration rejected; live configuration is unchanged");
        return false;
    }
    apply_globals(state, doc);
    bool any = false;
    bool ok  = apply_doc(state, doc, &any);
    config_doc_free(doc);
    if (!ok) {
        log_error("Configuration was valid, but one or more outputs failed to apply it");
        return false;
    }
    log_info("Configuration loaded from %s", path);
    return true;
}

/* One output that just appeared: re-read the file and give it its block, or
 * the built-in default. Live outputs are untouched. */
bool app_config_output(struct neowall_state *state, struct output_state *o) {
    if (!state || !o) return false;
    config_doc *doc = config_doc_load(state->config_path);
    struct wallpaper_config cfg;
    bool ok;
    if (doc && config_doc_for_output(doc, o->connector_name, o->model, &cfg)) {
        ok = output_apply_config(o, &cfg);
        config_free_wallpaper(&cfg);
    } else {
        log_info("No usable config for output %s, using built-in default", output_name(o));
        config_wallpaper_defaults(&cfg);
        ok = output_apply_config(o, &cfg);
    }
    config_doc_free(doc);
    return ok;
}
