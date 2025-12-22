#include <stdlib.h>
#include <math.h>
#include <GL/gl.h>
#include "neowall.h"

/* Generate blue noise texture
 * Blue noise has better distribution than white noise - useful for dithering
 * and better visual quality in shaders
 */

static unsigned int xorshift32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float random_float(unsigned int *state) {
    return (float)xorshift32(state) / (float)0xFFFFFFFF;
}

/* Simple blue noise approximation using void-and-cluster algorithm */
static void generate_blue_noise(unsigned char *data, int width, int height) {
    int total_pixels = width * height;
    unsigned char *binary = calloc(total_pixels, 1);
    float *energy = malloc(total_pixels * sizeof(float));
    
    if (!binary || !energy) {
        free(binary);
        free(energy);
        return;
    }
    
    // Initialize with random state
    unsigned int seed = 12345;
    
    // Simplified blue noise generation
    // Calculate energy for each pixel based on distance to other set pixels
    for (int i = 0; i < total_pixels; i++) {
        energy[i] = random_float(&seed);
    }
    
    // Sort-based approach for better distribution
    for (int threshold = 0; threshold < 256; threshold++) {
        for (int i = 0; i < total_pixels; i++) {
            float noise_val = random_float(&seed);
            
            // Add spatial correlation
            int x = i % width;
            int y = i / width;
            
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = (x + dx + width) % width;
                    int ny = (y + dy + height) % height;
                    int ni = ny * width + nx;
                    
                    if (binary[ni]) {
                        float dist = sqrtf((float)(dx*dx + dy*dy));
                        if (dist > 0.0f) {
                            noise_val += 0.3f / (dist * dist);
                        }
                    }
                }
            }
            
            energy[i] = noise_val;
        }
        
        // Find pixel with lowest energy and set it
        float min_energy = 1e10f;
        int min_idx = 0;
        for (int i = 0; i < total_pixels; i++) {
            if (!binary[i] && energy[i] < min_energy) {
                min_energy = energy[i];
                min_idx = i;
            }
        }
        
        if (threshold < 255) {
            binary[min_idx] = 1;
        }
        
        // Convert binary pattern to grayscale value
        for (int i = 0; i < total_pixels; i++) {
            int count = 0;
            int x = i % width;
            int y = i / width;
            
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = (x + dx + width) % width;
                    int ny = (y + dy + height) % height;
                    int ni = ny * width + nx;
                    count += binary[ni];
                }
            }
            
            data[i * 4 + 0] = (unsigned char)((count * 255) / 9);
            data[i * 4 + 1] = (unsigned char)((count * 255) / 9);
            data[i * 4 + 2] = (unsigned char)((count * 255) / 9);
            data[i * 4 + 3] = 255;
        }
    }
    
    free(binary);
    free(energy);
}

GLuint texture_create_blue_noise(int width, int height) {
    unsigned char *data = malloc(width * height * 4);
    if (!data) {
        return 0;
    }
    
    generate_blue_noise(data, width, height);
    
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