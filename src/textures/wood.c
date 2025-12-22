#include <stdlib.h>
#include <math.h>
#include <GL/gl.h>
#include "neowall.h"

/* Generate wood grain texture
 * Creates a realistic wood grain pattern useful for backgrounds
 */

static float fract_wood(float x) {
    return x - floorf(x);
}

static float hash_wood(float n) {
    return fract_wood(sinf(n) * 43758.5453123f);
}

static float noise_wood(float x, float y) {
    float px = floorf(x);
    float py = floorf(y);
    float fx = fract_wood(x);
    float fy = fract_wood(y);
    
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    
    float n = px + py * 157.0f;
    
    float a = hash_wood(n + 0.0f);
    float b = hash_wood(n + 1.0f);
    float c = hash_wood(n + 157.0f);
    float d = hash_wood(n + 158.0f);
    
    return a * (1.0f - fx) * (1.0f - fy) +
           b * fx * (1.0f - fy) +
           c * (1.0f - fx) * fy +
           d * fx * fy;
}

static float fbm_wood(float x, float y, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise_wood(x * frequency, y * frequency);
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    
    return value;
}

static float wood_pattern(float x, float y) {
    // Create wood grain pattern using concentric circles with noise
    float dist = sqrtf(x * x + y * y);
    
    // Add some warping
    dist += fbm_wood(x * 2.0f, y * 2.0f, 3) * 0.5f;
    
    // Create rings
    float rings = sinf(dist * 20.0f) * 0.5f + 0.5f;
    
    // Add fine grain details
    float grain = fbm_wood(x * 40.0f, y * 40.0f, 4) * 0.3f;
    
    return rings * 0.7f + grain * 0.3f;
}

GLuint texture_create_wood(int width, int height) {
    unsigned char *data = malloc(width * height * 4);
    if (!data) {
        return 0;
    }
    
    // Generate wood texture
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            
            float u = ((float)x / (float)width - 0.5f) * 2.0f;
            float v = ((float)y / (float)height - 0.5f) * 2.0f;
            
            // Generate wood pattern
            float wood = wood_pattern(u, v);
            
            // Wood color variations (brown tones)
            float base = 0.3f + wood * 0.4f;
            
            unsigned char r = (unsigned char)(base * 180.0f + 40.0f);
            unsigned char g = (unsigned char)(base * 120.0f + 30.0f);
            unsigned char b = (unsigned char)(base * 60.0f + 20.0f);
            
            data[idx + 0] = r;
            data[idx + 1] = g;
            data[idx + 2] = b;
            data[idx + 3] = 255;
        }
    }
    
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    glGenerateMipmap(GL_TEXTURE_2D);
    
    free(data);
    
    return texture;
}