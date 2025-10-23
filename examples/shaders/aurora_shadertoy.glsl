// Aurora Borealis (Shadertoy format)
// This shader uses Shadertoy's mainImage() convention
// Compatible with both Shadertoy and staticwall

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

// Aurora wave function
float auroraWave(vec2 uv, float t, float offset) {
    // Create flowing wave pattern
    float wave = sin(uv.x * 2.0 + offset + t * 0.3) * 0.5;
    wave += sin(uv.x * 3.5 - offset * 0.7 + t * 0.2) * 0.3;
    wave += sin(uv.x * 5.0 + offset * 1.3 - t * 0.4) * 0.2;

    // Add vertical movement
    wave += uv.y;

    // Add noise for organic feel
    wave += fbm(vec2(uv.x * 3.0 + t * 0.1, uv.y * 2.0 + offset)) * 0.3;

    return wave;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = fragCoord.xy / iResolution.xy;
    vec2 centeredUv = (fragCoord.xy - 0.5 * iResolution.xy) / iResolution.y;

    float t = iTime * 0.5;

    // Night sky background
    vec3 skyColor = vec3(0.01, 0.02, 0.05);

    // Add stars
    float stars = 0.0;
    for (int i = 0; i < 50; i++) {
        float fi = float(i);
        vec2 starPos = vec2(
            hash(vec2(fi, 0.0)),
            hash(vec2(fi, 1.0))
        );
        starPos = starPos * 2.0 - 1.0;
        starPos.y *= 0.5;
        starPos.y += 0.3;

        float dist = length(centeredUv - starPos);
        float twinkle = sin(t * 2.0 + fi * 10.0) * 0.3 + 0.7;
        stars += (0.002 / (dist + 0.001)) * twinkle;
    }

    vec3 color = skyColor + vec3(1.0, 1.0, 1.0) * stars * 0.3;

    // Create multiple aurora layers
    vec3 auroraColor = vec3(0.0);

    // Main green aurora
    float aurora1 = auroraWave(centeredUv, t, 0.0);
    float auroral1Intensity = exp(-abs(aurora1) * 3.0);
    auroral1Intensity *= smoothstep(0.8, 0.3, uv.y);

    vec3 green = vec3(0.1, 1.0, 0.3);
    auroraColor += green * auroral1Intensity * 0.8;

    // Secondary blue-green layer
    float aurora2 = auroraWave(centeredUv, t * 1.3, 2.5);
    float aurora2Intensity = exp(-abs(aurora2) * 4.0);
    aurora2Intensity *= smoothstep(0.9, 0.2, uv.y);

    vec3 blueGreen = vec3(0.2, 0.8, 0.9);
    auroraColor += blueGreen * aurora2Intensity * 0.6;

    // Pink/purple highlights
    float aurora3 = auroraWave(centeredUv, t * 0.8, 5.0);
    float aurora3Intensity = exp(-abs(aurora3) * 5.0);
    aurora3Intensity *= smoothstep(0.85, 0.25, uv.y);

    vec3 pink = vec3(1.0, 0.2, 0.8);
    auroraColor += pink * aurora3Intensity * 0.4;

    // Add vertical rays
    float rays = 0.0;
    for (int i = 0; i < 10; i++) {
        float fi = float(i);
        float rayX = -1.0 + fi * 0.22;
        float rayOffset = sin(t * 0.5 + fi) * 0.1;

        float rayDist = abs(centeredUv.x - (rayX + rayOffset));
        float rayIntensity = 0.05 / (rayDist + 0.05);

        // Fade based on y position
        float yFade = smoothstep(0.8, -0.3, centeredUv.y);
        rayIntensity *= yFade;

        // Flickering
        float flicker = sin(t * 3.0 + fi * 2.0) * 0.3 + 0.7;
        rayIntensity *= flicker;

        rays += rayIntensity;
    }

    auroraColor += vec3(0.1, 0.7, 0.5) * rays * 0.2;

    // Add shimmer effect
    float shimmer = fbm(vec2(centeredUv.x * 5.0 + t * 0.3, centeredUv.y * 3.0 - t * 0.2));
    shimmer = pow(shimmer, 2.0);
    auroraColor += vec3(0.5, 1.0, 0.7) * shimmer * 0.15 * smoothstep(0.7, 0.3, uv.y);

    // Combine with sky
    color += auroraColor;

    // Add atmospheric glow on horizon
    float horizonGlow = exp(-abs(centeredUv.y + 0.3) * 5.0);
    color += vec3(0.1, 0.3, 0.2) * horizonGlow * 0.3;

    // Darken bottom for ground silhouette
    float ground = smoothstep(-0.4, -0.5, centeredUv.y);
    color *= 1.0 - ground * 0.7;

    // Subtle vignette
    float vignette = 1.0 - length(centeredUv * 0.5);
    vignette = smoothstep(0.3, 1.0, vignette);
    color *= vignette * 0.5 + 0.5;

    fragColor = vec4(color, 1.0);
}
