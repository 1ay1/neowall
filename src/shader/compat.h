/* compat.h — make Shadertoy (GLSL ES 3.0, WebGL) source compile as
 * desktop GLSL 330 core. Pure string work, no GL. It handles what real
 * exported shaders do that desktop drivers reject:
 *   - a stray `#version` (we emit our own; two are an error);
 *   - `iChannelResolution[n]` used as a vec2 (it's a vec3 here): `.xy` added;
 *   - `uniform float iTime;` and friends redeclared by the shader: dropped
 *     (the engine provides them; a second declaration is an error).
 *   - `precision highp float;` lines: dropped (harmless in ES, an error in
 *     some desktop drivers before 330).
 *   - file-scope `const T x = <expr>;` where <expr> isn't a constant
 *     expression in desktop GLSL (it calls a function like cos(), or uses
 *     a const that was itself demoted): `const` is dropped so it becomes an
 *     ordinary global initialised at startup. Values are the same.
 * Comments and string literals are never touched. */
#ifndef NW_COMPAT_H
#define NW_COMPAT_H

#include "neowall/base.h"

/* Returns a new string (free it), or NULL on OOM. */
char *nw_compat_fix(const char *src);

#endif
