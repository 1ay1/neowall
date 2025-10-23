#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function for randomness
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Noise function
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

// Hexagonal grid pattern
float hexGrid(vec2 p) {
    vec2 h = vec2(1.0, 1.73205);
    vec2 a = mod(p, h) - h * 0.5;
    vec2 b = mod(p - h * 0.5, h) - h * 0.5;
    return min(dot(a, a), dot(b, b));
}

// Digital glitch effect
vec2 glitchOffset(vec2 uv, float intensity) {
    float glitchLine = step(0.98, hash(vec2(floor(uv.y * 20.0), floor(time * 10.0))));
    float glitchOffset = (hash(vec2(floor(time * 5.0), floor(uv.y * 10.0))) - 0.5) * 0.1;
    return vec2(glitchOffset * glitchLine * intensity, 0.0);
}

// Scan line effect
float scanLine(vec2 uv, float time) {
    return 0.5 + 0.5 * sin(uv.y * 300.0 + time * 5.0);
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    uv.x *= resolution.x / resolution.y;

    // Add glitch displacement
    float glitchIntensity = step(0.95, noise(vec2(time * 2.0, 0.0))) * 0.5;
    uv += glitchOffset(uv, glitchIntensity);

    // Animated grid
    vec2 gridUV = uv * 20.0;
    gridUV.x += time * 0.5;
    gridUV.y -= time * 0.3;

    // Hexagonal grid
    float hex = hexGrid(gridUV);
    float hexGrid = smoothstep(0.05, 0.08, hex);

    // Perpendicular grid lines
    vec2 grid = abs(fract(gridUV - 0.5) - 0.5) / fwidth(gridUV);
    float gridLine = min(grid.x, grid.y);
    float verticalLines = smoothstep(0.0, 2.0, gridLine);

    // Combine grids
    float gridPattern = 1.0 - min(hexGrid, verticalLines);

    // Neon glow colors
    vec3 cyan = vec3(0.0, 1.0, 1.0);
    vec3 magenta = vec3(1.0, 0.0, 1.0);
    vec3 yellow = vec3(1.0, 1.0, 0.0);

    // Color cycling based on position and time
    float colorShift = sin(uv.x * 3.0 + time) * 0.5 + 0.5;
    vec3 gridColor = mix(cyan, magenta, colorShift);
    gridColor = mix(gridColor, yellow, sin(uv.y * 2.0 + time * 0.5) * 0.5 + 0.5);

    // Apply grid pattern with glow
    vec3 color = gridColor * gridPattern * 0.8;

    // Add glowing moving lines
    float movingLine1 = abs(sin(uv.y * 10.0 - time * 3.0));
    movingLine1 = smoothstep(0.9, 1.0, movingLine1);
    color += cyan * movingLine1 * 2.0;

    float movingLine2 = abs(sin(uv.x * 8.0 + time * 2.0));
    movingLine2 = smoothstep(0.92, 1.0, movingLine2);
    color += magenta * movingLine2 * 1.5;

    // Digital noise
    float digitalNoise = hash(uv * 100.0 + time * 10.0) * 0.1;
    color += vec3(digitalNoise);

    // Glitch blocks
    float glitchBlock = step(0.97, hash(vec2(floor(uv.x * 10.0), floor(time * 3.0))));
    vec3 glitchColor = vec3(
        hash(vec2(time, uv.y)),
        hash(vec2(time + 1.0, uv.y)),
        hash(vec2(time + 2.0, uv.y))
    );
    color = mix(color, glitchColor, glitchBlock * 0.3);

    // Chromatic aberration effect
    float aberration = glitchIntensity * 0.01;
    float r = texture2D(vec4(color, 1.0), uv + vec2(aberration, 0.0)).r;
    float b = texture2D(vec4(color, 1.0), uv - vec2(aberration, 0.0)).b;

    // Pulsing vignette
    vec2 vignetteUV = gl_FragCoord.xy / resolution.xy - 0.5;
    float vignette = 1.0 - dot(vignetteUV, vignetteUV) * 0.5;
    vignette *= 0.8 + 0.2 * sin(time * 2.0);
    color *= vignette;

    // Scan lines
    float scan = scanLine(uv, time);
    color *= 0.9 + 0.1 * scan;

    // Data streams (vertical moving rectangles)
    float dataStream = step(0.98, hash(vec2(floor(uv.x * 30.0), floor(time * 5.0 - uv.y * 20.0))));
    color += vec3(0.0, 1.0, 0.5) * dataStream * 0.5;

    // Overall brightness and contrast
    color = pow(color, vec3(0.9)); // Gamma correction
    color *= 1.2; // Boost brightness

    gl_FragColor = vec4(color, 1.0);
}
