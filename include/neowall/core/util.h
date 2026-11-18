/* neowall/core/util.h — small helpers (src/core/util.c). */
#ifndef NEOWALL_CORE_UTIL_H
#define NEOWALL_CORE_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* Parse a non-negative decimal index from user or disk input. Rejects empty
 * strings, any non-digit and overflow, which atoi() can't report. */
bool neowall_parse_index(const char *s, long *out);

/* Longest accepted --ease-ms. Long enough for any deliberate ease, short
 * enough that a typo can't leave a wallpaper crawling for hours. */
#define NEOWALL_EASE_MS_MAX 60000u

/* Parse an ease duration: whole milliseconds, an optional `ms` suffix, at
 * most NEOWALL_EASE_MS_MAX. */
bool neowall_parse_ease_ms(const char *s, unsigned *out);

/* Expand a leading `~` to $HOME into `expanded`. False if it wouldn't fit;
 * treat that as a failure, never use a truncated path. */
bool expand_path(const char *path, char *expanded, size_t size);

/* Find `rel` (e.g. "shaders/x.glsl") under neowall's data dirs, in order:
 * $XDG_DATA_HOME/neowall, each $XDG_DATA_DIRS entry + /neowall, then the
 * compiled-in NEOWALL_DATADIR. False if none exists or a path doesn't fit. */
bool neowall_find_data(const char *rel, char *out, size_t size);

/* Call `fn` on each existing data dir, same order, until it returns true. */
bool neowall_each_data_dir(bool (*fn)(const char *dir, void *ctx), void *ctx);

/* A colour palette for shaders (`palette` in the config, `render --palette`).
 * `spec` is either inline colours ("#1a1b26 #7aa2f7 ...", any separators) or
 * a path to a file containing them (~ expanded; anything that isn't a 6-digit
 * hex colour is ignored, so a CSS/kitty/ghostty theme file works as-is).
 * Writes up to `max` RGB triplets in 0..1, returns how many (0 on error). */
#define NW_PALETTE_MAX 16
int neowall_palette_parse(const char *spec, float (*out)[3], int max);

/* Copy src into a fixed-size field. Safe when src IS dst. NULL clears it. */
void config_str_set(char *dst, size_t dst_size, const char *src);

/* $XDG_RUNTIME_DIR/neowall, else /tmp/neowall-<uid>: created 0700 and
 * checked to be a real dir we own (not a symlink). NULL if neither is safe.
 * Static buffer. */
const char *neowall_secure_runtime_dir(void);

float ease_in_out_cubic(float t);

#endif /* NEOWALL_CORE_UTIL_H */
