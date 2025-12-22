#include <stdlib.h>
#include <math.h>
#include <GL/gl.h>
#include "neowall.h"

/* Generate grayscale noise texture
 * Single channel noise that's useful for many effects
 */

static float hash_gray(float n) {
    return fmodf(sinf(n) * 43758.5453123f, 1.0f);
}

static float fract_gray(float x) {
    return x - floorf(x);
}

static float noise_gray(float x, float y) {
    float px = floorf(x);
    float py = floorf(y);
    float fx = fract_gray(x);
    float fy = fract_gray(y);
    
    // Smoothstep interpolation
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    
    float n = px + py * 157.0f;
    
    float a = hash_gray(n + 0.0f);
    float b = hash_gray(n + 1.0f);
    float c = hash_gray(n + 157.0f);
    float d = hash_gray(n + 158.0f);
    
    float res = a * (1.0f - fx) * (1.0f - fy) +
                b * fx * (1.0f - fy) +
                c * (1.0f - fx) * fy +
                d * fx * fy;
    
    return res;
}

static float fbm_gray(float x, float y, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise_gray(x * frequency, y * frequency);
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    
    return value;
}

GLuint texture_create_gray_noise(int width, int height) {
    unsigned char *data = malloc(width * height * 4);
    if (!data) {
        return 0;
    }
    
    // Generate grayscale noise
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            
            float u = (float)x / (float)width;
            float v = (float)y / (float)height;
            
            // High quality multi-octave noise
            float gray = fbm_gray(u * 10.0f, v * 10.0f, 5);
            
            unsigned char value = (unsigned char)(gray * 255.0f);
            
            data[idx + 0] = value;
            data[idx + 1] = value;
            data[idx + 2] = value;
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