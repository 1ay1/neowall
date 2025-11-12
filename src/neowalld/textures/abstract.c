#include <stdlib.h>
#include <math.h>
#include <GLES2/gl2.h>
#include "neowall.h"

/* Generate abstract colorful texture
 * Creates a Voronoi-based abstract pattern useful for artistic backgrounds
 */

static float fract_abstract(float x) {
    return x - floorf(x);
}

static float hash_abstract(float n) __attribute__((unused));
static float hash_abstract(float n) {
    return fract_abstract(sinf(n) * 43758.5453123f);
}

static void hash22_abstract(float x, float y, float *out_x, float *out_y) {
    float n = x + y * 157.0f;
    *out_x = fract_abstract(sinf(n) * 43758.5453123f);
    *out_y = fract_abstract(cosf(n) * 73156.8493217f);
}

static float hash13_abstract(float x, float y, float z) {
    float n = x + y * 57.0f + z * 113.0f;
    return fract_abstract(sinf(n) * 43758.5453123f);
}

/* Voronoi distance field */
static float voronoi(float x, float y, float *cell_id) {
    float px = floorf(x);
    float py = floorf(y);
    float fx = fract_abstract(x);
    float fy = fract_abstract(y);
    
    float min_dist = 10.0f;
    float closest_id = 0.0f;
    
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            float cell_x = px + (float)i;
            float cell_y = py + (float)j;
            
            float point_x, point_y;
            hash22_abstract(cell_x, cell_y, &point_x, &point_y);
            
            float dx = (float)i + point_x - fx;
            float dy = (float)j + point_y - fy;
            float dist = sqrtf(dx * dx + dy * dy);
            
            if (dist < min_dist) {
                min_dist = dist;
                closest_id = hash13_abstract(cell_x, cell_y, 0.0f);
            }
        }
    }
    
    *cell_id = closest_id;
    return min_dist;
}

/* Generate colorful abstract pattern */
static void abstract_pattern(float u, float v, unsigned char *r, unsigned char *g, unsigned char *b) {
    float cell_id;
    float scale = 6.0f;
    
    // Primary voronoi pattern
    float dist1 = voronoi(u * scale, v * scale, &cell_id);
    
    // Secondary pattern at different scale
    float cell_id2;
    float dist2 = voronoi(u * scale * 2.3f + 100.0f, v * scale * 2.3f + 200.0f, &cell_id2);
    
    // Color based on cell ID
    float hue = cell_id * 6.28318f;
    float sat = 0.6f + cell_id2 * 0.4f;
    float val = 0.5f + dist1 * 0.5f;
    
    // HSV to RGB conversion
    float c = val * sat;
    float x = c * (1.0f - fabsf(fmodf(hue / 1.047198f, 2.0f) - 1.0f));
    float m = val - c;
    
    float r_f = 0.0f, g_f = 0.0f, b_f = 0.0f;
    
    if (hue < 1.047198f) {
        r_f = c; g_f = x; b_f = 0.0f;
    } else if (hue < 2.094395f) {
        r_f = x; g_f = c; b_f = 0.0f;
    } else if (hue < 3.141593f) {
        r_f = 0.0f; g_f = c; b_f = x;
    } else if (hue < 4.188790f) {
        r_f = 0.0f; g_f = x; b_f = c;
    } else if (hue < 5.235988f) {
        r_f = x; g_f = 0.0f; b_f = c;
    } else {
        r_f = c; g_f = 0.0f; b_f = x;
    }
    
    // Add some variation with second pattern
    r_f = r_f * 0.7f + dist2 * 0.3f;
    g_f = g_f * 0.7f + (1.0f - dist2) * 0.3f;
    b_f = b_f * 0.7f + cell_id2 * 0.3f;
    
    *r = (unsigned char)((r_f + m) * 255.0f);
    *g = (unsigned char)((g_f + m) * 255.0f);
    *b = (unsigned char)((b_f + m) * 255.0f);
}

GLuint texture_create_abstract(int width, int height) {
    unsigned char *data = malloc(width * height * 4);
    if (!data) {
        return 0;
    }
    
    // Generate abstract pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            
            float u = (float)x / (float)width;
            float v = (float)y / (float)height;
            
            unsigned char r, g, b;
            abstract_pattern(u, v, &r, &g, &b);
            
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