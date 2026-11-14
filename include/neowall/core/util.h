/* neowall/core/util.h — small helpers (src/core/util.c). */
#ifndef NEOWALL_CORE_UTIL_H
#define NEOWALL_CORE_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* Parse a non-negative decimal index from user or disk input. Rejects empty
 * strings, any non-digit and overflow, which atoi() can't report. */
bool neowall_parse_index(const char *s, long *out);

/* Expand a leading `~` to $HOME into `expanded`. False if it wouldn't fit;
 * treat that as a failure, never use a truncated path. */
bool expand_path(const char *path, char *expanded, size_t size);

/* Find `rel` (e.g. "shaders/x.glsl") under neowall's data dirs, in order:
 * $XDG_DATA_HOME/neowall, each $XDG_DATA_DIRS entry + /neowall, then the
 * compiled-in NEOWALL_DATADIR. False if none exists or a path doesn't fit. */
bool neowall_find_data(const char *rel, char *out, size_t size);

/* Call `fn` on each existing data dir, same order, until it returns true. */
bool neowall_each_data_dir(bool (*fn)(const char *dir, void *ctx), void *ctx);

/* Copy src into a fixed-size field. Safe when src IS dst. NULL clears it. */
void config_str_set(char *dst, size_t dst_size, const char *src);

/* $XDG_RUNTIME_DIR/neowall, else /tmp/neowall-<uid>: created 0700 and
 * checked to be a real dir we own (not a symlink). NULL if neither is safe.
 * Static buffer. */
const char *neowall_secure_runtime_dir(void);

float ease_in_out_cubic(float t);

#endif /* NEOWALL_CORE_UTIL_H */
