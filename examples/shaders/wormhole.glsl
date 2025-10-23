#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function
float hash(vec2 p) {
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

// Rotation matrix
mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
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

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution.xy) / resolution.y;

    float t = time * 0.5;

    // Convert to polar coordinates
    float angle = atan(uv.y, uv.x);
    float radius = length(uv);

    // Create tunnel effect
    float tunnel = 1.0 / (radius + 0.2);

    // Warp spacetime
    float warp = t * 2.0 - tunnel * 3.0;

    // Spiral pattern
    float spiral = angle + warp + sin(tunnel * 2.0 + t) * 0.5;

    // Create rings
    float rings = sin(tunnel * 10.0 - t * 3.0) * 0.5 + 0.5;
    rings = pow(rings, 3.0);

    // Add noise for turbulence
    float turbulence = noise(vec2(spiral * 2.0, tunnel * 5.0 + t));

    // Base color - purple/blue spacetime
    vec3 color = vec3(0.0);

    // Ring colors with warping
    vec3 ringColor1 = vec3(0.5, 0.2, 1.0);  // Purple
    vec3 ringColor2 = vec3(0.2, 0.8, 1.0);  // Cyan
    vec3 ringColor3 = vec3(1.0, 0.3, 0.7);  // Pink

    float colorPhase = fract(tunnel * 2.0 - t * 0.5);
    vec3 tunnelColor;
    if (colorPhase < 0.33) {
        tunnelColor = mix(ringColor1, ringColor2, colorPhase * 3.0);
    } else if (colorPhase < 0.66) {
        tunnelColor = mix(ringColor2, ringColor3, (colorPhase - 0.33) * 3.0);
    } else {
        tunnelColor = mix(ringColor3, ringColor1, (colorPhase - 0.66) * 3.0);
    }

    color = tunnelColor * rings * 0.8;

    // Add grid lines
    float gridX = abs(fract(spiral / 0.5) - 0.5);
    float gridY = abs(fract(tunnel * 5.0 - t * 2.0) - 0.5);

    float grid = step(gridX, 0.05) + step(gridY, 0.05);
    grid = min(grid, 1.0);
    color += vec3(0.3, 0.6, 1.0) * grid * 0.3 * (1.0 - radius * 0.5);

    // Add energy streams along spiral
    float stream = sin(spiral * 8.0 - t * 5.0) * 0.5 + 0.5;
    stream = pow(stream, 10.0);
    color += vec3(1.0, 0.8, 0.5) * stream * 0.5;

    // Add stars being pulled in
    for (int i = 0; i < 30; i++) {
        float fi = float(i);

        // Star position
        float starAngle = hash(vec2(fi, 0.0)) * 6.28318;
        float starDist = hash(vec2(fi, 1.0)) * 2.0 + 0.5;

        // Pull stars toward center over time
        starDist = mod(starDist - t * 0.3, 2.5);

        vec2 starPos = vec2(cos(starAngle), sin(starAngle)) * starDist;

        // Add spiral motion
        float spiralEffect = starDist * 2.0 + t * 2.0;
        starPos = starPos * rot(spiralEffect);

        float dist = length(uv - starPos);

        // Star brightness
        float star = 0.005 / (dist + 0.001);

        // Twinkle
        float twinkle = sin(t * 5.0 + fi * 10.0) * 0.3 + 0.7;

        // Color shift as stars get pulled in
        float colorShift = 1.0 - starDist / 2.5;
        vec3 starColor = mix(
            vec3(1.0, 1.0, 1.0),
            vec3(0.5, 0.8, 1.0),
            colorShift
        );

        color += starColor * star * twinkle * 0.5;
    }

    // Add central bright spot (event horizon)
    float center = exp(-radius * radius * 8.0);
    color += vec3(1.0, 0.9, 1.0) * center * 2.0;

    // Add radial glow
    float radialGlow = exp(-radius * 2.0);
    color += tunnelColor * radialGlow * 0.3;

    // Vignette to enhance depth
    float vignette = 1.0 - radius * 0.7;
    vignette = smoothstep(0.0, 1.0, vignette);
    color *= vignette;

    // Add noise/turbulence overlay
    color += turbulence * 0.1;

    gl_FragColor = vec4(color, 1.0);
}
