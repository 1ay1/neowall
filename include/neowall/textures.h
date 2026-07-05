#ifndef TEXTURES_H
#define TEXTURES_H

#include <GL/gl.h>

/* Procedural texture generators for iChannel inputs
 * These are the most commonly used textures in Shadertoy shaders
 */

/* RGBA noise texture - most common Shadertoy texture
 * Contains independent noise in all 4 channels
 * Typical size: 256x256 or 512x512
 */
GLuint texture_create_rgba_noise(int width, int height);

/* Grayscale noise texture
 * High quality multi-octave noise in grayscale
 * Useful for displacement maps and effects
 * Typical size: 256x256
 */
GLuint texture_create_gray_noise(int width, int height);

/* Blue noise texture
 * Better distributed than white noise, ideal for dithering
 * Reduces banding artifacts in shaders
 * Typical size: 128x128 or 256x256
 */
GLuint texture_create_blue_noise(int width, int height);

/* Wood grain texture
 * Procedural wood pattern with realistic grain
 * Useful for natural backgrounds
 * Typical size: 256x256
 */
GLuint texture_create_wood(int width, int height);

/* Abstract colorful texture
 * Voronoi-based pattern with colors
 * Good for artistic/abstract backgrounds
 * Typical size: 256x256
 */
GLuint texture_create_abstract(int width, int height);

/* Font atlas texture
 * A crisp 8x12 monospace bitmap font baked into a 128x72 texture as a
 * 16x6 grid of ASCII cells (codes 32..127). Sampled with GL_NEAREST so text
 * stays sharp. Use the nwFontGlyph/nwText helpers in the shader stdlib.
 */
GLuint texture_create_font_atlas(int width, int height);

/* Texture name constants for configuration */
#define TEXTURE_NAME_RGBA_NOISE  "rgba_noise"
#define TEXTURE_NAME_GRAY_NOISE  "gray_noise"
#define TEXTURE_NAME_BLUE_NOISE  "blue_noise"
#define TEXTURE_NAME_WOOD        "wood"
#define TEXTURE_NAME_ABSTRACT    "abstract"
#define TEXTURE_NAME_FONT        "font"

/* Default texture sizes */
#define DEFAULT_TEXTURE_SIZE 256

#endif /* TEXTURES_H */