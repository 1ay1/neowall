/* neowall/output/state_file.h — what each output shows, on disk
 * (src/output/state_file.c). Read by `neowall current` / `neowall list`, and to
 * resume the cycle position after a restart. */
#ifndef NEOWALL_OUTPUT_STATE_FILE_H
#define NEOWALL_OUTPUT_STATE_FILE_H

#include <stdbool.h>
#include <stddef.h>

/* $XDG_STATE_HOME/neowall/state, with the usual fallbacks. Empty if no safe
 * place exists. Static buffer. */
const char *get_state_file_path(void);

bool write_wallpaper_state(const char *output_name, const char *wallpaper_path,
                           const char *mode, int cycle_index, int cycle_total,
                           const char *status);
/* Atomically remove one output, or keep only the given output names. An
 * absent file or name is a successful no-op. */
bool remove_wallpaper_state(const char *output_name);
bool prune_wallpaper_state(const char *const *output_names, size_t output_count);
int restore_cycle_index_from_state(const char *output_name);

/* The CLI's views: print the state / the cycle list to stdout. */
bool read_wallpaper_state(void);
bool read_cycle_list(void);

bool write_cycle_list(const char *output_name, char **paths, size_t count, size_t current_index);

#endif /* NEOWALL_OUTPUT_STATE_FILE_H */
