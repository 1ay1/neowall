// Matrix Digital Rain - Textured Version
// Works with iChannel0 texture or procedurally generated fallback
// GLSL ES 1.0 compatible

#define RAIN_SPEED 1.75 // Speed of rain droplets
#define DROP_SIZE  3.0  // Higher value lowers the size of individual droplets

// Simple hash function for randomness
float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// Generate random character appearance
float rchar(vec2 outer, vec2 inner, float globalTime) {
    vec2 seed = floor(inner * 4.0) + outer.y;

    // Some columns update faster
    if (rand(vec2(outer.y, 23.0)) > 0.98) {
        seed += floor((globalTime + rand(vec2(outer.y, 49.0))) * 3.0);
    }

    return float(rand(seed) > 0.5);
}

// Procedural noise fallback for when texture is dark/empty
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = rand(i);
    float b = rand(i + vec2(1.0, 0.0));
    float c = rand(i + vec2(0.0, 1.0));
    float d = rand(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 position = fragCoord.xy / iResolution.xy;
    vec2 uv = vec2(position.x, position.y);
    position.x /= iResolution.x / iResolution.y;
    float globalTime = iTime * RAIN_SPEED;

    float scaledown = DROP_SIZE;
    vec4 result = vec4(0.0);

    // First rain layer
    float rx = fragCoord.x / (40.0 * scaledown);
    float mx = 40.0 * scaledown * fract(position.x * 30.0 * scaledown);

    if (mx > 12.0 * scaledown) {
        result = vec4(0.0);
    } else {
        float x = floor(rx);
        float r1x = floor(fragCoord.x / 15.0);

        float ry = position.y * 600.0 + rand(vec2(x, x * 3.0)) * 100000.0 +
                   globalTime * rand(vec2(r1x, 23.0)) * 120.0;
        float my = mod(ry, 15.0);

        if (my > 12.0 * scaledown) {
            result = vec4(0.0);
        } else {
            float y = floor(ry / 15.0);
            float b = rchar(vec2(rx, floor(ry / 15.0)), vec2(mx, my) / 12.0, globalTime);
            float col = max(mod(-y, 24.0) - 4.0, 0.0) / 20.0;

            vec3 c;
            if (col < 0.8) {
                c = vec3(0.0, col / 0.8, 0.0);
            } else {
                c = mix(vec3(0.0, 1.0, 0.0), vec3(1.0), (col - 0.8) / 0.2);
            }

            result = vec4(c * b, 1.0);
        }
    }

    // Second rain layer (offset for depth)
    position.x += 0.05;

    scaledown = DROP_SIZE;
    rx = fragCoord.x / (40.0 * scaledown);
    mx = 40.0 * scaledown * fract(position.x * 30.0 * scaledown);

    if (mx > 12.0 * scaledown) {
        result += vec4(0.0);
    } else {
        float x = floor(rx);
        float r1x = floor(fragCoord.x / 12.0);

        float ry = position.y * 700.0 + rand(vec2(x, x * 3.0)) * 100000.0 +
                   globalTime * rand(vec2(r1x, 23.0)) * 120.0;
        float my = mod(ry, 15.0);

        if (my > 12.0 * scaledown) {
            result += vec4(0.0);
        } else {
            float y = floor(ry / 15.0);
            float b = rchar(vec2(rx, floor(ry / 15.0)), vec2(mx, my) / 12.0, globalTime);
            float col = max(mod(-y, 24.0) - 4.0, 0.0) / 20.0;

            vec3 c;
            if (col < 0.8) {
                c = vec3(0.0, col / 0.8, 0.0);
            } else {
                c = mix(vec3(0.0, 1.0, 0.0), vec3(1.0), (col - 0.8) / 0.2);
            }

            result += vec4(c * b, 1.0);
        }
    }

    // Sample texture from iChannel0
    vec3 texColor = texture2D(iChannel0, uv).rgb;
    float texBrightness = length(texColor);

    // If texture is too dark (like night_glow), use procedural noise instead
    if (texBrightness < 0.15) {
        // Use procedural noise
        float texNoise = noise(uv * 50.0 + vec2(0.0, iTime * 0.5));
        texColor = vec3(0.0, texNoise, 0.0);
        texBrightness = texNoise;
    }

    // Combine rain with texture
    result.rgb = result.rgb * texBrightness + 0.22 * vec3(0.0, texColor.g, 0.0);

    // Add blue tint to shadows
    if (result.b < 0.5) {
        result.b = result.g * 0.5;
    }

    // Add subtle glow
    float glow = length(result.rgb) * 0.1;
    result.rgb += vec3(0.0, glow, glow * 0.3);

    fragColor = vec4(result.rgb, 1.0);
}
