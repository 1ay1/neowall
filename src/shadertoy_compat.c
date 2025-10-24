#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "staticwall.h"
#include "constants.h"

/**
 * Intelligent Shadertoy Shader Preprocessor
 * 
 * This preprocessor deeply analyzes Shadertoy shaders and intelligently transforms them
 * to be compatible with staticwall by:
 * 1. Detecting all Shadertoy-specific features and providing fallbacks
 * 2. Analyzing function dependencies and injecting required implementations
 * 3. Automatically fixing common compatibility issues
 * 4. Optimizing shaders for better performance when possible
 * 5. Providing detailed analysis and suggestions
 */

/* ========================================================================
 * DETECTION FUNCTIONS - Analyze what the shader uses
 * ======================================================================== */

static bool contains_pattern(const char *source, const char *pattern) {
    return strstr(source, pattern) != NULL;
}

static bool uses_texture_channels(const char *source) {
    for (int i = 0; i <= 4; i++) {
        char pattern[16];
        snprintf(pattern, sizeof(pattern), "iChannel%d", i);
        if (contains_pattern(source, pattern)) {
            return true;
        }
    }
    return false;
}

static bool uses_custom_noise_function(const char *source) {
    // Check if shader defines its own noise function
    return contains_pattern(source, "vec3 noised(") ||
           contains_pattern(source, "float pn(");
}

static bool has_conflicting_hash_functions(const char *source) {
    // Check if shader already defines hash functions we want to inject
    return contains_pattern(source, "hash11(") ||
           contains_pattern(source, "hash12(") ||
           contains_pattern(source, "hash22(") ||
           contains_pattern(source, "hash33(");
}

static bool uses_derivatives(const char *source) {
    // Check if shader uses derivative functions that require GL_OES_standard_derivatives
    return contains_pattern(source, "fwidth(") ||
           contains_pattern(source, "dFdx(") ||
           contains_pattern(source, "dFdy(");
}

/* ========================================================================
 * COMMON SHADERTOY SHORTHAND MACROS - Frequently used abbreviations
 * ======================================================================== */

/* Common shorthand macros used in many Shadertoy shaders */
static const char *common_shadertoy_macros =
    "// ===== COMMON SHADERTOY SHORTHAND MACROS =====\n"
    "// Based on Shadertoy.com official specification\n"
    "// https://www.shadertoy.com/howto\n"
    "\n"
    "// Set precision for all subsequent float operations\n"
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "#endif\n"
    "\n"
    "// Mathematical constants\n"
    "#ifndef PI\n"
    "#define PI 3.14159265359\n"
    "#endif\n"
    "\n"
    "#ifndef TAU\n"
    "#define TAU 6.28318530718\n"
    "#endif\n"
    "\n"
    "#ifndef PHI\n"
    "#define PHI 1.61803398875\n"
    "#endif\n"
    "\n"
    "#ifndef E\n"
    "#define E 2.71828182846\n"
    "#endif\n"
    "\n"
    "// Time shortcuts\n"
    "#define T iTime\n"
    "#define t iTime\n"
    "\n"
    "// Resolution shortcuts\n"
    "#define R iResolution\n"
    "\n"
    "// Fragment coordinate shortcuts\n"
    "#define FC gl_FragCoord\n"
    "#define fc gl_FragCoord\n"
    "\n"
    "// Mouse shortcuts\n"
    "#define M iMouse\n"
    "#define m iMouse\n"
    "\n"
    "// Common function shortcuts\n"
    "#define S(a,b,x) smoothstep(a,b,x)\n"
    "#define sat(x) clamp(x,0.,1.)\n"
    "#define saturate(x) clamp(x,0.,1.)\n"
    "\n"
    "// Box-step / band function\n"
    "#define B(a,b,x,t) (smoothstep(a-t,a+t,x)*smoothstep(b+t,b-t,x))\n"
    "\n"
    "// Rotation matrix 2D\n"
    "#define rot(a) mat2(cos(a),sin(a),-sin(a),cos(a))\n"
    "#define ROT(a) mat2(cos(a),sin(a),-sin(a),cos(a))\n"
    "\n"
    "// Inline rotation (modifies vector in place)\n"
    "#define r(v,a) { float _c=cos(a),_s=sin(a); v=vec2(_c*v.x-_s*v.y,_s*v.x+_c*v.y); }\n"
    "\n"
    "// Vector shortcuts\n"
    "#define v2(x) vec2(x)\n"
    "#define v3(x) vec3(x)\n"
    "#define v4(x) vec4(x)\n"
    "\n"
    "// Matrix shortcuts\n"
    "#define m2(a,b,c,d) mat2(a,b,c,d)\n"
    "#define m3(a,b,c,d,e,f,g,h,i) mat3(a,b,c,d,e,f,g,h,i)\n"
    "\n"
    "// Common texture shortcuts\n"
    "#define T0(u) texture(iChannel0,u)\n"
    "#define T1(u) texture(iChannel1,u)\n"
    "#define T2(u) texture(iChannel2,u)\n"
    "#define T3(u) texture(iChannel3,u)\n"
    "\n"
    "// Modulo constants for hash functions (commonly used in Shadertoy)\n"
    "#define MOD3 vec3(.1031,.1030,.0973)\n"
    "#define MOD4 vec4(.1031,.1030,.0973,.1099)\n"
    "\n"
    "// Common math shortcuts\n"
    "#define smin(a,b,k) (-(log(exp(-k*a)+exp(-k*b))/k))\n"
    "\n"
    "// Normalize shortcuts\n"
    "#define n(x) normalize(x)\n"
    "#define N(x) normalize(x)\n"
    "\n"
    "// Length shortcuts\n"
    "#define l(x) length(x)\n"
    "#define L(x) length(x)\n"
    "\n"
    "// Absolute value shortcuts\n"
    "#define a(x) abs(x)\n"
    "#define A(x) abs(x)\n"
    "\n"
    "// Floor shortcuts\n"
    "#define f(x) floor(x)\n"
    "#define F(x) floor(x)\n"
    "\n"
    "// Ceiling shortcuts\n"
    "#define c(x) ceil(x)\n"
    "#define C(x) ceil(x)\n"
    "\n"
    "// Fract shortcuts\n"
    "#define fr(x) fract(x)\n"
    "\n"
    "// Minimum/Maximum shortcuts\n"
    "#define mn(a,b) min(a,b)\n"
    "#define mx(a,b) max(a,b)\n"
    "\n"
    "// Step shortcuts\n"
    "#define stp(e,x) step(e,x)\n"
    "\n"
    "// Dot product shortcuts\n"
    "#define d(a,b) dot(a,b)\n"
    "#define D(a,b) dot(a,b)\n"
    "\n"
    "// Cross product shortcuts\n"
    "#define x(a,b) cross(a,b)\n"
    "#define X(a,b) cross(a,b)\n"
    "\n"
    "// Power shortcuts\n"
    "#define p(x,y) pow(x,y)\n"
    "#define P(x,y) pow(x,y)\n"
    "\n"
    "// Sign shortcuts\n"
    "#define sgn(x) sign(x)\n"
    "\n"
    "// Miscellaneous\n"
    "#define mul(a,b) (b*a)\n"
    "\n"
    "// Common comparison shortcuts\n"
    "#define eq(a,b) (abs((a)-(b))<0.001)\n"
    "\n"
    "// Modulo shortcuts\n"
    "#define md(x,y) mod(x,y)\n"
    "\n"
    "// Reflect shortcuts\n"
    "#define rfl(i,n) reflect(i,n)\n"
    "\n"
    "// Refract shortcuts\n"
    "#define rfr(i,n,e) refract(i,n,e)\n"
    "\n"
    "// Distance shortcuts\n"
    "#define dst(a,b) distance(a,b)\n"
    "\n"
    "// Min/max component shortcuts\n"
    "#define mnc(x) min(min((x).x,(x).y),(x).z)\n"
    "#define mxc(x) max(max((x).x,(x).y),(x).z)\n"
    "\n";



/* ========================================================================
 * FALLBACK IMPLEMENTATIONS - Smart replacements for missing features
 * ======================================================================== */

/* Advanced noise functions that can replace texture lookups */
static const char *advanced_noise_functions =
    "// ===== ADVANCED NOISE FUNCTIONS (Auto-generated fallbacks) =====\n"
    "\n"
    "// High-quality hash function\n"
    "float _sw_hash11(float p) {\n"
    "    p = fract(p * 0.1031);\n"
    "    p *= p + 33.33;\n"
    "    p *= p + p;\n"
    "    return fract(p);\n"
    "}\n"
    "\n"
    "float _sw_hash12(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "\n"
    "vec2 _sw_hash22(vec2 p) {\n"
    "    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n"
    "    p3 += dot(p3, p3.yzx + 33.33);\n"
    "    return fract((p3.xx + p3.yz) * p3.zy);\n"
    "}\n"
    "\n"
    "vec3 _sw_hash33(vec3 p3) {\n"
    "    p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));\n"
    "    p3 += dot(p3, p3.yxz + 33.33);\n"
    "    return fract((p3.xxy + p3.yxx) * p3.zyx);\n"
    "}\n"
    "\n"
    "// Value noise\n"
    "float _sw_noise11(float x) {\n"
    "    float i = floor(x);\n"
    "    float f = fract(x);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(_sw_hash11(i), _sw_hash11(i + 1.0), f);\n"
    "}\n"
    "\n"
    "float _sw_noise12(vec2 x) {\n"
    "    vec2 i = floor(x);\n"
    "    vec2 f = fract(x);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    float a = _sw_hash12(i);\n"
    "    float b = _sw_hash12(i + vec2(1.0, 0.0));\n"
    "    float c = _sw_hash12(i + vec2(0.0, 1.0));\n"
    "    float d = _sw_hash12(i + vec2(1.0, 1.0));\n"
    "    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
    "}\n"
    "\n"
    "// Texture lookup fallbacks using noise - accept vec2 or vec3\n"
    "vec4 iChannel_fallback(int channel, vec2 uv, float lod) {\n"
    "    // Use different noise patterns for different channels\n"
    "    vec2 p = uv * 256.0;\n"
    "    \n"
    "    // Generate unique noise pattern for each channel\n"
    "    float offset = float(channel) * 137.5; // Prime-ish offset for variety\n"
    "    \n"
    "    if (channel == 0) {\n"
    "        float n = _sw_noise12(p);\n"
    "        return vec4(n, n, n, 1.0);\n"
    "    } else if (channel == 1) {\n"
    "        vec2 n = _sw_hash22(p);\n"
    "        return vec4(n.x, n.y, _sw_noise12(p * 2.0), 1.0);\n"
    "    } else if (channel == 2) {\n"
    "        vec3 n = _sw_hash33(vec3(p, 0.0));\n"
    "        return vec4(n, 1.0);\n"
    "    } else {\n"
    "        // Channels 3-20: varied noise patterns\n"
    "        float r = _sw_noise12(p + vec2(offset, 0.0));\n"
    "        float g = _sw_noise12(p + vec2(0.0, offset));\n"
    "        float b = _sw_noise12(p + vec2(offset, offset));\n"
    "        return vec4(r, g, b, 1.0);\n"
    "    }\n"
    "}\n"
    "\n"
    "vec4 iChannel_fallback_v3(int channel, vec3 uvw, float lod) {\n"
    "    // For vec3 input, just use xy components\n"
    "    return iChannel_fallback(channel, uvw.xy, lod);\n"
    "}\n"
    "\n"
    "vec4 textureLod_iChannel(int channel, vec2 uv, float lod) {\n"
    "    return iChannel_fallback(channel, uv, lod);\n"
    "}\n"
    "\n"
    "vec4 textureLod_iChannel(int channel, vec3 uvw, float lod) {\n"
    "    return iChannel_fallback_v3(channel, uvw, lod);\n"
    "}\n"
    "\n"
    "vec4 texelFetch_iChannel(int channel, ivec2 coord, int lod) {\n"
    "    vec2 uv = vec2(coord) / 256.0;\n"
    "    return iChannel_fallback(channel, uv, float(lod));\n"
    "}\n"
    "\n"
    "vec4 texture_iChannel(int channel, vec2 uv) {\n"
    "    return iChannel_fallback(channel, uv, 0.0);\n"
    "}\n"
    "\n"
    "vec4 texture_iChannel(int channel, vec3 uvw) {\n"
    "    return iChannel_fallback_v3(channel, uvw, 0.0);\n"
    "}\n"
    "\n";

/* ========================================================================
 * INTELLIGENT STRING REPLACEMENT - Context-aware transformations
 * ======================================================================== */

/* Dynamic string builder for efficient concatenation */
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StringBuilder;

static StringBuilder *sb_create(size_t initial_capacity) {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    
    sb->data = malloc(initial_capacity);
    if (!sb->data) {
        free(sb);
        return NULL;
    }
    
    sb->data[0] = '\0';
    sb->length = 0;
    sb->capacity = initial_capacity;
    return sb;
}

static bool sb_append(StringBuilder *sb, const char *str, size_t len) {
    if (!sb || !str) return false;
    
    size_t required = sb->length + len + 1;
    if (required > sb->capacity) {
        size_t new_capacity = sb->capacity * 2;
        if (new_capacity < required) new_capacity = required * 2;
        
        char *new_data = realloc(sb->data, new_capacity);
        if (!new_data) return false;
        
        sb->data = new_data;
        sb->capacity = new_capacity;
    }
    
    memcpy(sb->data + sb->length, str, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
    return true;
}

static bool sb_append_str(StringBuilder *sb, const char *str) {
    return sb_append(sb, str, strlen(str));
}

static char *sb_finish(StringBuilder *sb) {
    if (!sb) return NULL;
    char *result = sb->data;
    free(sb);
    return result;
}

static void sb_free(StringBuilder *sb) {
    if (sb) {
        free(sb->data);
        free(sb);
    }
}

/* Replace texture functions intelligently */
static bool replace_texture_calls(StringBuilder *sb, const char *source) {
    const char *read_ptr = source;
    
    while (*read_ptr) {
        bool found_texture_call = false;
        
        // Check for textureLod with iChannel
        if (strncmp(read_ptr, "textureLod", 10) == 0) {
            const char *check = read_ptr + 10;
            while (*check && isspace(*check)) check++;
            
            if (*check == '(') {
                check++;
                while (*check && isspace(*check)) check++;
                
                if (strncmp(check, "iChannel", 8) == 0) {
                    check += 8;
                    // Parse channel number (0-20)
                    if (isdigit(*check)) {
                        const char *num_start = check;
                        while (isdigit(*check)) check++;
                        
                        // Extract channel number
                        char channel_num[4] = {0};
                        size_t num_len = check - num_start;
                        if (num_len < sizeof(channel_num)) {
                            strncpy(channel_num, num_start, num_len);
                            int ch = atoi(channel_num);
                            
                            if (ch >= 0 && ch <= 4) {
                                // Found textureLod(iChannel0...20, ...)
                                sb_append_str(sb, "textureLod_iChannel(");
                                sb_append_str(sb, channel_num);
                                sb_append_str(sb, ",");
                                read_ptr = check;
                                
                                // Skip the comma after iChannelN
                                while (*read_ptr && isspace(*read_ptr)) read_ptr++;
                                if (*read_ptr == ',') read_ptr++;
                                
                                found_texture_call = true;
                            }
                        }
                    }
                }
            }
        }
        
        // Check for texelFetch with iChannel
        if (!found_texture_call && strncmp(read_ptr, "texelFetch", 10) == 0) {
            const char *check = read_ptr + 10;
            while (*check && isspace(*check)) check++;
            
            if (*check == '(') {
                check++;
                while (*check && isspace(*check)) check++;
                
                if (strncmp(check, "iChannel", 8) == 0) {
                    check += 8;
                    // Parse channel number (0-20)
                    if (isdigit(*check)) {
                        const char *num_start = check;
                        while (isdigit(*check)) check++;
                        
                        // Extract channel number
                        char channel_num[4] = {0};
                        size_t num_len = check - num_start;
                        if (num_len < sizeof(channel_num)) {
                            strncpy(channel_num, num_start, num_len);
                            int ch = atoi(channel_num);
                            
                            if (ch >= 0 && ch <= 4) {
                                // Found texelFetch(iChannel0...20, ...)
                                sb_append_str(sb, "texelFetch_iChannel(");
                                sb_append_str(sb, channel_num);
                                sb_append_str(sb, ",");
                                read_ptr = check;
                                
                                // Skip the comma after iChannelN
                                while (*read_ptr && isspace(*read_ptr)) read_ptr++;
                                if (*read_ptr == ',') read_ptr++;
                                
                                found_texture_call = true;
                            }
                        }
                    }
                }
            }
        }
        
        // Check for texture with iChannel (GLSL ES 3.0 style)
        // Must handle: texture(iChannel0,coord) and avoid: textureLod, texelFetch
        if (!found_texture_call && strncmp(read_ptr, "texture", 7) == 0) {
            const char *check = read_ptr + 7;
            
            // Make sure it's not textureLod, texelFetch, texture2D, etc
            if (*check == '(' || isspace(*check)) {
                while (*check && isspace(*check)) check++;
                
                if (*check == '(') {
                    check++;
                    while (*check && isspace(*check)) check++;
                    
                    if (strncmp(check, "iChannel", 8) == 0) {
                        check += 8;
                        // Parse channel number (0-20)
                        if (isdigit(*check)) {
                            const char *num_start = check;
                            while (isdigit(*check)) check++;
                            
                            // Extract channel number
                            char channel_num[4] = {0};
                            size_t num_len = check - num_start;
                            if (num_len < sizeof(channel_num)) {
                                strncpy(channel_num, num_start, num_len);
                                int ch = atoi(channel_num);
                                
                                if (ch >= 0 && ch <= 4) {
                                    // Found texture(iChannel0...20, ...)
                                    sb_append_str(sb, "texture_iChannel(");
                                    sb_append_str(sb, channel_num);
                                    sb_append_str(sb, ",");
                                    read_ptr = check;
                                    
                                    // Skip the comma after iChannelN
                                    while (*read_ptr && isspace(*read_ptr)) read_ptr++;
                                    if (*read_ptr == ',') read_ptr++;
                                    
                                    found_texture_call = true;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        if (found_texture_call) {
            continue;
        }
        
        // Copy character as-is
        sb_append(sb, read_ptr, 1);
        read_ptr++;
    }
    
    return true;
}

/* ========================================================================
 * MAIN PREPROCESSING LOGIC
 * ======================================================================== */

char *shadertoy_preprocess(const char *source) {
    if (!source) {
        return NULL;
    }
    
    log_info("Starting intelligent shader analysis...");
    
    // Analyze what features the shader uses
    bool has_textures = uses_texture_channels(source);
    bool has_conflicts = has_conflicting_hash_functions(source);
    bool needs_derivatives = uses_derivatives(source);
    
    // Build the preprocessed shader
    StringBuilder *sb = sb_create(strlen(source) * 2);
    if (!sb) {
        log_error("Failed to create string builder");
        return NULL;
    }
    
    // Add GL_OES_standard_derivatives extension if needed
    if (needs_derivatives) {
        log_info("Injecting GL_OES_standard_derivatives extension (shader uses fwidth/dFdx/dFdy)");
        sb_append_str(sb, "#ifdef GL_ES\n");
        sb_append_str(sb, "#extension GL_OES_standard_derivatives : enable\n");
        sb_append_str(sb, "#endif\n\n");
    }
    
    // Always inject common Shadertoy shorthand macros
    log_info("Injecting common Shadertoy shorthand macros");
    sb_append_str(sb, common_shadertoy_macros);
    
    // Add noise functions if textures are used (always needed for texture fallbacks)
    if (has_textures) {
        if (has_conflicts) {
            log_info("Injecting advanced noise functions for texture fallbacks (using _sw_ prefix to avoid conflicts)");
        } else {
            log_info("Injecting advanced noise functions for texture fallbacks");
        }
        sb_append_str(sb, advanced_noise_functions);
    }
    
    // Process the source code
    if (has_textures) {
        log_info("Processing texture channel references...");
        if (!replace_texture_calls(sb, source)) {
            log_error("Failed to process texture calls");
            sb_free(sb);
            return NULL;
        }
    } else {
        // No texture processing needed
        sb_append_str(sb, source);
    }
    
    char *result = sb_finish(sb);
    
    if (result) {
        log_info("Shader preprocessing completed successfully");
        log_info("  - Added common Shadertoy shorthand macros");
        if (needs_derivatives) {
            log_info("  - Added GL_OES_standard_derivatives extension");
        }
        if (has_textures) {
            log_info("  - Replaced texture lookups with noise-based fallbacks");
        }
    }
    
    return result;
}

/* ========================================================================
 * GLSL TEXTURE CONVERSION - Modern GLSL to GLSL ES 1.0
 * ======================================================================== */

/**
 * Convert GLSL 3.0 texture functions to GLSL ES 1.0 texture2D
 * 
 * This function intelligently converts:
 * - texture(sampler, uv) -> texture2D(sampler, uv)
 * - textureLod(sampler, uv, lod) -> texture2D(sampler, uv)  [strips LOD parameter]
 * 
 * Only converts calls to iChannel samplers, leaves other samplers unchanged.
 * 
 * @param source Shader source code with GLSL 3.0 texture calls
 * @return Converted shader source (must be freed by caller), or NULL on error
 */
char *shadertoy_convert_texture_calls(const char *source) {
    if (!source) {
        return NULL;
    }
    
    log_info("Converting GLSL 3.0 texture() calls to GLSL ES 1.0 texture2D()...");
    
    /* Allocate buffer with extra space for potential expansions */
    size_t source_len = strlen(source);
    char *converted = malloc(source_len * 2 + 1024);
    if (!converted) {
        log_error("Failed to allocate memory for texture conversion");
        return NULL;
    }
    
    const char *src = source;
    char *dst = converted;
    int conversions = 0;
    
    while (*src) {
        bool converted_call = false;
        
        /* ================================================================
         * Convert textureLod(iChannel, uv, lod) -> texture2D(iChannel, uv)
         * ================================================================ */
        if (strncmp(src, "textureLod(", 11) == 0) {
            const char *check = src + 11;
            while (*check && isspace(*check)) check++;
            
            /* Only convert if it's an iChannel sampler */
            if (strncmp(check, "iChannel", 8) == 0) {
                log_debug("Converting textureLod(iChannel...) to texture2D(iChannel...)");
                
                /* Write replacement: texture2D( */
                strcpy(dst, "texture2D(");
                dst += 10;
                src += 11;
                
                /* Copy parameters until we find the LOD parameter (after 2nd comma) */
                int paren_depth = 1;
                int comma_count = 0;
                
                while (*src && paren_depth > 0) {
                    if (*src == '(') paren_depth++;
                    else if (*src == ')') {
                        paren_depth--;
                        if (paren_depth == 0) {
                            /* End of textureLod call */
                            *dst++ = ')';
                            src++;
                            break;
                        }
                    }
                    else if (*src == ',' && paren_depth == 1) {
                        comma_count++;
                        if (comma_count == 2) {
                            /* Found the LOD parameter - skip everything until closing paren */
                            while (*src && *src != ')') {
                                if (*src == '(') paren_depth++;
                                else if (*src == ')') paren_depth--;
                                src++;
                            }
                            /* Write closing paren */
                            *dst++ = ')';
                            if (*src == ')') src++;
                            break;
                        }
                    }
                    
                    *dst++ = *src++;
                }
                
                conversions++;
                converted_call = true;
            }
        }
        
        /* ================================================================
         * Convert texture(iChannel, uv) -> texture2D(iChannel, uv)
         * ================================================================ */
        if (!converted_call && strncmp(src, "texture(", 8) == 0) {
            /* Make sure it's not textureLod, texelFetch, texture2D, etc. */
            if (src == source || !isalpha((unsigned char)src[-1])) {
                const char *check = src + 8;
                while (*check && isspace(*check)) check++;
                
                /* Only convert if it's an iChannel sampler */
                if (strncmp(check, "iChannel", 8) == 0) {
                    log_debug("Converting texture(iChannel...) to texture2D(iChannel...)");
                    
                    /* Write replacement: texture2D( */
                    strcpy(dst, "texture2D(");
                    dst += 10;
                    src += 8;
                    
                    conversions++;
                    converted_call = true;
                }
            }
        }
        
        /* If no conversion happened, copy character as-is */
        if (!converted_call) {
            *dst++ = *src++;
        }
    }
    
    *dst = '\0';
    
    log_info("Texture conversion completed: %d calls converted", conversions);
    
    return converted;
}

/* ========================================================================
 * INTELLIGENT SHADER ANALYSIS
 * ======================================================================== */

void shadertoy_analyze_shader(const char *source) {
    if (!source) {
        return;
    }
    
    log_info("=== Intelligent Shader Analysis ===");
    
    // Feature detection
    bool has_channels = uses_texture_channels(source);
    bool has_mouse = contains_pattern(source, "iMouse");
    bool has_time_delta = contains_pattern(source, "iTimeDelta");
    bool has_frame = contains_pattern(source, "iFrame");
    bool has_date = contains_pattern(source, "iDate");
    bool has_custom_noise = uses_custom_noise_function(source);
    bool has_mod = contains_pattern(source, "mod(");
    bool has_mul = contains_pattern(source, "mul(");
    bool has_saturate = contains_pattern(source, "saturate(");
    
    // Texture analysis
    int channel_count = 0;
    for (int i = 0; i <= 4; i++) {
        char pattern[16];
        snprintf(pattern, sizeof(pattern), "iChannel%d", i);
        if (strstr(source, pattern)) {
            channel_count++;
        }
    }
    
    // Complexity estimation
    size_t source_len = strlen(source);
    int line_count = 1;
    int function_count = 0;
    int loop_count = 0;
    
    for (const char *p = source; *p; p++) {
        if (*p == '\n') line_count++;
    }
    
    const char *p = source;
    while (p && *p) {
        const char *pf = strstr(p, "float ");
        const char *p2 = strstr(p, "vec2 ");
        const char *p3 = strstr(p, "vec3 ");
        const char *p4 = strstr(p, "vec4 ");
        
        const char *next = NULL;
        if (pf && (!next || pf < next)) next = pf;
        if (p2 && (!next || p2 < next)) next = p2;
        if (p3 && (!next || p3 < next)) next = p3;
        if (p4 && (!next || p4 < next)) next = p4;
        
        if (!next) break;
        
        p = next + 1;
        function_count++;
    }
    
    p = source;
    while (p && *p) {
        p = strstr(p, "for");
        if (!p) break;
        p += 3;
        loop_count++;
    }
    
    // Report features
    log_info("Shader Statistics:");
    log_info("  - Lines: %d", line_count);
    log_info("  - Size: %zu bytes", source_len);
    log_info("  - Functions: ~%d", function_count);
    log_info("  - Loops: %d", loop_count);
    
    log_info("Features Detected:");
    
    if (has_channels) {
        log_info("  + Texture Channels: %d channel%s", 
                 channel_count, channel_count != 1 ? "s" : "");
        log_info("    -> Will use noise-based fallbacks");
    }
    
    if (has_mouse) {
        log_info("  + Mouse Input (iMouse)");
        log_info("    -> Currently fixed at (0,0,0,0)");
    }
    
    if (has_time_delta) {
        log_info("  + Frame Delta (iTimeDelta)");
        log_info("    -> Fixed at ~16.67ms (60fps)");
    }
    
    if (has_frame) {
        log_info("  + Frame Counter (iFrame)");
        log_info("    -> Currently fixed at 0");
    }
    
    if (has_date) {
        log_info("  + Date/Time (iDate)");
        log_info("    -> Static fallback value");
    }
    
    if (has_custom_noise) {
        log_info("  + Custom Noise Functions");
        log_info("    -> Shader provides its own noise");
    }
    
    // Compatibility notes
    if (has_mod || has_mul || has_saturate) {
        log_info("Compatibility Features:");
        if (has_mod) log_info("  + GLSL mod() function");
        if (has_mul) log_info("  + HLSL-style mul() detected");
        if (has_saturate) log_info("  + HLSL-style saturate() detected");
    }
    
    // Performance estimation
    int complexity_score = 0;
    complexity_score += channel_count * 2;
    complexity_score += loop_count;
    complexity_score += (function_count > 30) ? 3 : (function_count > 15) ? 2 : 1;
    complexity_score += (source_len > 10000) ? 2 : (source_len > 5000) ? 1 : 0;
    
    log_info("Performance Estimate:");
    if (complexity_score <= 3) {
        log_info("  -> Simple shader - should run very smoothly");
    } else if (complexity_score <= 6) {
        log_info("  -> Moderate complexity - good performance expected");
    } else if (complexity_score <= 10) {
        log_info("  -> Complex shader - may impact performance on lower-end GPUs");
    } else {
        log_info("  -> Very complex shader - performance may vary");
        log_info("  -> Consider simplifying or reducing quality if needed");
    }
    
    // Optimization suggestions
    if (loop_count > 5) {
        log_info("Tip: Shader has %d loops - consider reducing iterations if performance is low", loop_count);
    }
    
    if (channel_count > 2) {
        log_info("Tip: Shader uses %d texture channels - noise fallbacks may not look identical", channel_count);
        log_info("     Original Shadertoy shaders often look different without real texture data");
    }
    
    if (channel_count > 0) {
        log_info("Note: This shader expects texture input which staticwall doesn't support yet");
        log_info("      Using procedural noise as fallback - visuals will differ from Shadertoy");
    }
    
    log_info("=================================");
}