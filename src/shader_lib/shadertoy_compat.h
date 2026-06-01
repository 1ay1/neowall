/* Shadertoy → desktop-GL source compatibility transforms.
 *
 * Pure string processing, no OpenGL dependency, so it can be unit-tested
 * headless. shader_multipass.c calls shadertoy_compat_fix() while wrapping
 * each pass; the GLSL-ES builtin shims (texture2D -> texture, etc.) live as
 * #define macros in the wrapper prefix there, while the rewrites that can't be
 * expressed as macros live here.
 */

#ifndef SHADERTOY_COMPAT_H
#define SHADERTOY_COMPAT_H

/**
 * Normalize Shadertoy GLSL for compilation under `#version 330 core`.
 *
 * Comment- and string-literal-aware: tokens inside // ... , block comments, or
 * "..." string literals are never rewritten. Currently performs:
 *   - removal of any stray `#version` directive in the user source (the caller
 *     emits its own, and two are a compile error);
 *   - `iChannelResolution[n]` -> `iChannelResolution[n].xy` where no component
 *     access already follows (the uniform is a vec3 on desktop).
 *
 * @param source NUL-terminated GLSL source (may be NULL).
 * @return newly allocated transformed string (caller frees), or NULL if
 *         source was NULL or allocation failed.
 */
char *shadertoy_compat_fix(const char *source);

#endif /* SHADERTOY_COMPAT_H */
