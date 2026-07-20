/*
 * vibe_impl.c — the single translation unit that compiles the vendored,
 * single-header libvibe implementation into neowall.
 *
 * libvibe (https://github.com/1ay1/vibe) is an stb-style single-header
 * library: its entire implementation lives in vibe.h, guarded by
 * VIBE_IMPLEMENTATION. Every other TU includes the header normally and gets
 * just the declarations; this file is the one place VIBE_IMPLEMENTATION is
 * defined, so the implementation is emitted exactly once.
 *
 * Do not add code here — to update libvibe, re-vendor upstream vibe.h into
 * include/neowall/config/vibe.h.
 */
#define VIBE_IMPLEMENTATION
#include "neowall/config/vibe.h"
