#ifndef SHADERTOY_COMPAT_H
#define SHADERTOY_COMPAT_H

#include <stdbool.h>

/**
 * Shadertoy Compatibility Layer
 * 
 * Provides preprocessing and analysis for Shadertoy-format shaders
 * to make them compatible with neowall's rendering system.
 */

/**
 * Preprocess a Shadertoy shader to make it compatible with neowall.
 * 
 * This function:
 * - Detects usage of texture channels (iChannel0-3)
 * - Replaces texture lookups with noise-based fallbacks
 * - Handles textureLod and texelFetch calls
 * 
 * @param source Original Shadertoy shader source code
 * @return Preprocessed shader source (must be freed by caller), or NULL on error
 */
char *shadertoy_preprocess(const char *source);

/**
 * Analyze a Shadertoy shader and log information about its features.
 * 
 * Provides user feedback about:
 * - Detected features (textures, mouse input, etc.)
 * - Complexity estimation
 * - Performance expectations
 * 
 * @param source Shader source code to analyze
 */
void shadertoy_analyze_shader(const char *source);

/**
 * Convert GLSL 3.0 texture functions to GLSL ES 1.0 texture2D.
 * 
 * This function intelligently converts modern GLSL texture calls to
 * GLSL ES 1.0 compatible calls:
 * - texture(iChannel, uv) -> texture2D(iChannel, uv)
 * - textureLod(iChannel, uv, lod) -> texture2D(iChannel, uv)
 * 
 * Only converts calls to iChannel samplers, leaves other samplers unchanged.
 * 
 * @param source Shader source code with GLSL 3.0 texture calls
 * @return Converted shader source (must be freed by caller), or NULL on error
 */
char *shadertoy_convert_texture_calls(const char *source);

#endif /* SHADERTOY_COMPAT_H */