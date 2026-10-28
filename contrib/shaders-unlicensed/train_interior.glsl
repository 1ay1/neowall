// Aurora Dreams - Northern Lights over Mountains
// A mesmerizing aurora borealis with mountain silhouettes and lake reflection
// Optimized for NeoWall

#define PI 3.14159265

// ============== Noise Functions ==============

float hash(float p) {
    return fract(sin(p) * 43758.5453123);
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash2(i);
    float b = hash2(i + vec2(1.0, 0.0));
    float c = hash2(i + vec2(0.0, 1.0));
    float d = hash2(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float f = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; i++) {
        f += amp * noise(p);
        p *= 2.03;
        amp *= 0.5;
    }
    return f;
}

// ============== Aurora ==============

vec3 aurora(vec2 uv, float time) {
    vec3 col = vec3(0.0);

    // Multiple aurora layers
    for (float i = 0.0; i < 3.0; i++) {
        float t = time * (0.3 + i * 0.1);

        // Flowing curtain effect
        float x = uv.x * (2.0 + i * 0.5) + t * 0.2;
        float y = uv.y * 3.0 - i * 0.3;

        // Wavy pattern
        float wave = sin(x * 3.0 + t) * 0.3;
        wave += sin(x * 5.0 - t * 1.3) * 0.2;
        wave += sin(x * 7.0 + t * 0.7) * 0.1;

        // Noise for organic feel
        float n = fbm(vec2(x * 0.5, y + t * 0.1));

        // Aurora curtain shape
        float curtain = smoothstep(0.3 + wave, 0.5 + wave, uv.y + n * 0.2);
        curtain *= smoothstep(0.9, 0.6, uv.y);
        curtain *= smoothstep(-0.2, 0.3, uv.y + wave * 0.5);

        // Intensity variation
        float intensity = (0.5 + 0.5 * sin(x * 2.0 + t * 2.0)) * curtain;
        intensity *= 0.5 + 0.5 * n;

        // Aurora colors - greens, teals, and hints of purple
        vec3 auroraCol;
        float colorShift = sin(x * 0.5 + t * 0.3 + i) * 0.5 + 0.5;

        if (i < 1.0) {
            auroraCol = mix(vec3(0.1, 0.8, 0.4), vec3(0.0, 0.6, 0.8), colorShift);
        } else if (i < 2.0) {
            auroraCol = mix(vec3(0.0, 0.7, 0.6), vec3(0.3, 0.2, 0.8), colorShift);
        } else {
            auroraCol = mix(vec3(0.5, 0.1, 0.7), vec3(0.1, 0.9, 0.5), colorShift);
        }

        col += auroraCol * intensity * (0.5 - i * 0.1);
    }

    return col;
}

// ============== Stars ==============

vec3 stars(vec2 uv, float time) {
    vec3 col = vec3(0.0);

    // Layer of stars
    for (float i = 0.0; i < 2.0; i++) {
        vec2 p = uv * (100.0 + i * 50.0);
        vec2 id = floor(p);
        vec2 f = fract(p);

        float star = hash2(id + i * 100.0);

        if (star > 0.97) {
            vec2 center = vec2(hash2(id + 1.0), hash2(id + 2.0));
            float d = length(f - center);

            // Twinkling
            float twinkle = sin(time * (5.0 + star * 10.0) + star * 100.0) * 0.5 + 0.5;
            float brightness = smoothstep(0.1, 0.0, d) * twinkle;

            col += vec3(0.9, 0.95, 1.0) * brightness * (1.0 - i * 0.3);
        }
    }

    return col;
}

// ============== Mountains ==============

float mountain(vec2 uv, float offset, float scale, float sharpness) {
    float x = uv.x * scale + offset;

    float h = 0.0;
    h += sin(x * 0.5) * 0.3;
    h += sin(x * 1.3 + 1.0) * 0.2;
    h += sin(x * 2.7 + 2.0) * 0.1;
    h += noise(vec2(x * 2.0, 0.0)) * 0.15;

    // Sharpen peaks
    h = pow(abs(h), sharpness) * sign(h);

    return h;
}

float mountains(vec2 uv) {
    float m = 0.0;

    // Back layer - distant mountains
    float h1 = mountain(uv, 0.0, 1.5, 0.8) * 0.25 + 0.15;
    if (uv.y < h1) m = max(m, 0.3);

    // Middle layer
    float h2 = mountain(uv, 5.0, 2.0, 0.7) * 0.3 + 0.08;
    if (uv.y < h2) m = max(m, 0.6);

    // Front layer - closest mountains
    float h3 = mountain(uv, 10.0, 2.5, 0.6) * 0.2 + 0.02;
    if (uv.y < h3) m = max(m, 1.0);

    return m;
}

// ============== Water ==============

vec3 water(vec2 uv, vec3 skyColor, float time) {
    // Flip for reflection
    vec2 reflectUV = vec2(uv.x, -uv.y);

    // Water ripples
    float ripple = 0.0;
    ripple += sin(uv.x * 20.0 + time * 2.0) * 0.003;
    ripple += sin(uv.x * 35.0 - time * 1.5) * 0.002;
    ripple += sin((uv.x + uv.y * 5.0) * 15.0 + time) * 0.002;

    reflectUV.y += ripple;
    reflectUV.x += ripple * 0.5;

    // Get reflected sky/aurora color
    vec3 reflected = skyColor;

    // Darken and add blue tint for water
    reflected *= 0.6;
    reflected = mix(reflected, vec3(0.05, 0.1, 0.2), 0.3);

    // Fresnel - edges more reflective
    float fresnel = pow(1.0 - abs(uv.y), 3.0) * 0.5;
    reflected += fresnel * vec3(0.1, 0.15, 0.2);

    // Subtle shimmer
    float shimmer = noise(uv * 50.0 + time) * 0.05;
    reflected += shimmer;

    return reflected;
}

// ============== Main ==============

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord / iResolution.xy;
    uv.x *= iResolution.x / iResolution.y;

    // Center and scale
    uv.x -= 0.5 * iResolution.x / iResolution.y;
    uv.y -= 0.35;

    float time = iTime * 0.5;

    // === Sky ===
    vec3 col = vec3(0.0);

    // Night sky gradient
    vec3 skyTop = vec3(0.02, 0.02, 0.08);
    vec3 skyBottom = vec3(0.05, 0.08, 0.15);
    col = mix(skyBottom, skyTop, clamp(uv.y * 1.5, 0.0, 1.0));

    // Stars (only in upper part)
    if (uv.y > 0.1) {
        col += stars(uv, time);
    }

    // Aurora
    vec3 auroraCol = aurora(uv, time);
    col += auroraCol;

    // Store sky color for reflection
    vec3 skyWithAurora = col;

    // === Mountains ===
    float m = mountains(uv);
    if (m > 0.0) {
        // Mountain silhouette with slight gradient
        vec3 mountainCol = vec3(0.02, 0.02, 0.04) * (1.0 - m * 0.3);

        // Subtle edge glow from aurora
        float edge = smoothstep(0.0, 0.02, uv.y - mountain(uv, 10.0, 2.5, 0.6) * 0.2 - 0.02);
        mountainCol += auroraCol * 0.1 * (1.0 - edge);

        col = mountainCol;
    }

    // === Water/Lake ===
    float waterLine = -0.05;
    if (uv.y < waterLine) {
        // Reflection of sky and aurora
        vec2 reflectUV = vec2(uv.x, -uv.y + waterLine * 2.0);

        // Get reflected aurora
        vec3 reflectedAurora = aurora(reflectUV, time);
        vec3 reflectedSky = mix(skyBottom, skyTop, clamp(reflectUV.y * 1.5, 0.0, 1.0));
        reflectedSky += reflectedAurora;

        // Add stars reflection (dimmer)
        if (reflectUV.y > 0.1) {
            reflectedSky += stars(reflectUV, time) * 0.3;
        }

        col = water(uv, reflectedSky, time);

        // Mountain reflection
        float reflectedM = mountains(vec2(uv.x, -uv.y + waterLine * 2.0));
        if (reflectedM > 0.0) {
            vec3 mountainReflect = vec3(0.02, 0.02, 0.04) * (1.0 - reflectedM * 0.3);
            mountainReflect = mix(mountainReflect, col, 0.4); // Blend with water
            col = mountainReflect;
        }
    }

    // === Shooting Star (occasional) ===
    float shootTime = mod(time * 0.3, 8.0);
    if (shootTime < 0.5 && uv.y > 0.2) {
        vec2 shootStart = vec2(0.3 + hash(floor(time * 0.3)) * 0.4, 0.5);
        vec2 shootDir = normalize(vec2(1.0, -0.5));
        vec2 shootPos = shootStart + shootDir * shootTime * 2.0;

        float shootDist = length(uv - shootPos);
        float trail = smoothstep(0.1, 0.0, length(uv - shootPos + shootDir * 0.1));
        trail *= smoothstep(0.5, 0.0, shootTime);

        col += vec3(1.0, 0.95, 0.8) * trail * 0.5;
    }

    // === Post Processing ===

    // Subtle vignette
    vec2 q = fragCoord / iResolution.xy;
    float vignette = 1.0 - 0.3 * pow(length(q - 0.5) * 1.3, 2.0);
    col *= vignette;

    // Slight color grading - more teal in shadows
    col = mix(col, col * vec3(0.9, 1.0, 1.1), 0.2);

    // Gamma
    col = pow(col, vec3(0.9));

    // Subtle film grain
    float grain = (hash(dot(fragCoord, vec2(12.9898, 78.233)) + iTime) - 0.5) * 0.02;
    col += grain;

    // Fade in
    col *= smoothstep(0.0, 2.0, iTime);

    fragColor = vec4(col, 1.0);
}
