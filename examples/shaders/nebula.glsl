#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function
float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

// Smooth noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Fractional Brownian Motion
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < 6; i++) {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

// Rotation matrix
mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    float t = time * 0.1;

    // Slowly rotate the view
    uv = uv * rot(t * 0.1);

    // Deep space background
    vec3 color = vec3(0.01, 0.01, 0.03);

    // Create nebula cloud layers with different colors
    vec2 nebulaUv1 = uv * 2.0 + vec2(t * 0.3, t * 0.2);
    vec2 nebulaUv2 = uv * 3.0 - vec2(t * 0.2, t * 0.4);
    vec2 nebulaUv3 = uv * 1.5 + vec2(t * 0.4, -t * 0.3);

    float cloud1 = fbm(nebulaUv1);
    float cloud2 = fbm(nebulaUv2);
    float cloud3 = fbm(nebulaUv3);

    // Purple/pink nebula (main)
    vec3 nebula1Color = vec3(0.8, 0.2, 0.9);
    float nebula1 = pow(cloud1, 2.0);
    color += nebula1Color * nebula1 * 0.6;

    // Blue nebula
    vec3 nebula2Color = vec3(0.2, 0.5, 1.0);
    float nebula2 = pow(cloud2, 2.5);
    color += nebula2Color * nebula2 * 0.4;

    // Orange/red nebula (star formation regions)
    vec3 nebula3Color = vec3(1.0, 0.4, 0.1);
    float nebula3 = pow(cloud3, 3.0);
    color += nebula3Color * nebula3 * 0.5;

    // Add bright regions (star formation)
    float brightRegion = pow(cloud1 * cloud2, 3.0);
    color += vec3(1.0, 0.9, 0.8) * brightRegion * 2.0;

    // Dark dust clouds
    float dust = fbm(uv * 4.0 + vec2(t * 0.15, -t * 0.25));
    dust = pow(dust, 4.0);
    color *= 1.0 - dust * 0.6;

    // Add stars
    for (int i = 0; i < 100; i++) {
        float fi = float(i);

        vec2 starPos = vec2(
            hash(vec2(fi, 0.0)) * 2.0 - 1.0,
            hash(vec2(fi, 1.0)) * 2.0 - 1.0
        );

        float starSize = hash(vec2(fi, 2.0));
        float dist = length(uv - starPos);

        // Star glow
        float star = 0.002 * starSize / (dist + 0.001);

        // Twinkle
        float twinkle = sin(t * 5.0 + fi * 10.0) * 0.3 + 0.7;

        // Star color variation
        float colorVar = hash(vec2(fi, 3.0));
        vec3 starColor;
        if (colorVar < 0.3) {
            starColor = vec3(0.7, 0.8, 1.0);  // Blue stars (hot)
        } else if (colorVar < 0.6) {
            starColor = vec3(1.0, 1.0, 1.0);  // White stars
        } else if (colorVar < 0.85) {
            starColor = vec3(1.0, 0.9, 0.7);  // Yellow stars
        } else {
            starColor = vec3(1.0, 0.5, 0.3);  // Red giants
        }

        color += starColor * star * twinkle;
    }

    // Add distant stars (small)
    for (int i = 0; i < 200; i++) {
        float fi = float(i + 100);

        vec2 starPos = vec2(
            hash(vec2(fi, 0.0)) * 3.0 - 1.5,
            hash(vec2(fi, 1.0)) * 3.0 - 1.5
        );

        float dist = length(uv - starPos);
        float star = 0.0005 / (dist + 0.001);

        color += vec3(0.9, 0.9, 1.0) * star * 0.5;
    }

    // Add glowing filaments
    for (int i = 0; i < 5; i++) {
        float fi = float(i);

        vec2 filamentStart = vec2(
            cos(fi * 2.0 + t) * 0.5,
            sin(fi * 1.5 + t * 0.7) * 0.5
        );

        vec2 toFilament = uv - filamentStart;
        float filamentAngle = fi * 1.2 + t * 0.3;
        vec2 filamentDir = vec2(cos(filamentAngle), sin(filamentAngle));

        float along = dot(toFilament, filamentDir);
        float perp = length(toFilament - filamentDir * along);

        if (along > 0.0 && along < 0.6) {
            float filament = 0.005 / (perp + 0.003);
            filament *= smoothstep(0.6, 0.0, along);

            vec3 filamentColor = mix(
                vec3(0.5, 0.3, 1.0),
                vec3(1.0, 0.3, 0.5),
                fi / 5.0
            );

            color += filamentColor * filament * 0.3;
        }
    }

    // Add energy waves
    float wave = sin(length(uv) * 8.0 - t * 2.0) * 0.5 + 0.5;
    wave = pow(wave, 10.0);
    color += vec3(0.5, 0.7, 1.0) * wave * 0.3;

    // Lens flare from bright regions
    float lensFlare = 0.0;
    vec2 flareDir = normalize(uv);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        vec2 flarePos = -flareDir * (0.3 + fi * 0.2);
        float flareDist = length(uv - flarePos);
        lensFlare += 0.02 / (flareDist + 0.02) * brightRegion;
    }
    color += vec3(0.8, 0.6, 1.0) * lensFlare * 0.3;

    // Vignette
    float vignette = 1.0 - length(uv * 0.5);
    vignette = smoothstep(0.2, 1.0, vignette);
    color *= vignette * 0.8 + 0.2;

    // Enhance contrast
    color = pow(color, vec3(0.9));

    gl_FragColor = vec4(color, 1.0);
}
