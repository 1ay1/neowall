#ifndef NEOWALL_CONFIG_H
#define NEOWALL_CONFIG_H

/* The config file: read it, validate all of it, then answer "what should
 * this output show?". Pure parsing: no outputs, no GL, no daemon. Applying
 * the answers to live outputs is the app's job (app/configure.c).
 *
 *   config_doc *d = config_doc_load(path, &err);   // whole file checked here
 *   config_doc_for_output(d, "DP-1", "Dell U2720Q", &wc);
 *   config_doc_free(d);
 */

#include <stdbool.h>
#include <stddef.h>

#include "neowall/config/wallpaper.h"

typedef struct config_doc config_doc;

/* Daemon-wide settings at the root of the file. */
typedef struct {
    bool has_mouse_interaction, mouse_interaction;
    bool has_term_raw_input, term_raw_input;
} config_globals;

/* Read, parse and validate the whole file. NULL on any problem, logged
 * with the reason; nothing has been applied anywhere then. */
config_doc *config_doc_load(const char *path);
void config_doc_free(config_doc *doc);

config_globals config_doc_globals(const config_doc *doc);

/* The block for this output: its own (by connector, then model) or
 * `default`. Fills *out (free with config_free_wallpaper). False if no
 * block applies. */
bool config_doc_for_output(const config_doc *doc, const char *connector, const char *model,
                           struct wallpaper_config *out);

/* A last-resort config: an image found in the usual places, or empty. */
void config_builtin_default(struct wallpaper_config *out);
void config_wallpaper_defaults(struct wallpaper_config *out);

/* Write the starter config (copied from the data dir) at `path`. */
bool config_create_default(const char *path);
const char *config_get_default_path(void);

void config_free_wallpaper(struct wallpaper_config *config);

/* Edit distance between two short strings (typo suggestions). */
size_t config_edit_distance(const char *a, const char *b);

/* Fisher-Yates shuffle of an array of cycle path pointers (issue #47).
 * keep_first_at_zero=true preserves paths[0] (used on wrap so the just-shown
 * item doesn't repeat). The RNG is seeded lazily on first call. */
void config_shuffle_cycle_paths(char **paths, size_t n, bool keep_first_at_zero);

#endif /* NEOWALL_CONFIG_H */
