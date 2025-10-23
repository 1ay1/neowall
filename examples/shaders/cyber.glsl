#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function for randomness
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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

// Digital glitch effect
vec2 glitchOffset(vec2 uv, float intensity) {
    float glitchLine = step(0.985, hash(vec2(floor(uv.y * 30.0), floor(time * 8.0))));
    float glitchOffset = (hash(vec2(floor(time * 4.0), floor(uv.y * 15.0))) - 0.5) * 0.15;
    return vec2(glitchOffset * glitchLine * intensity, 0.0);
}

// Hex digit display (0-F)
float hexDigit(vec2 uv, float value) {
    uv = fract(uv) - 0.5;
    float pattern = 0.0;

    vec2 auv = abs(uv);
    if (auv.x < 0.3 && auv.y < 0.4) {
        float seg = step(auv.y, 0.35) * step(0.25, auv.y);
        seg += step(auv.x, 0.25) * step(0.15, auv.x);
        pattern = seg * step(0.1, hash(vec2(value)));
    }

    return pattern;
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    uv.x *= resolution.x / resolution.y;

    // Add glitch displacement
    float glitchIntensity = step(0.97, noise(vec2(time * 1.5, 0.0))) * 0.6;
    uv += glitchOffset(uv, glitchIntensity);

    // Grid with movement
    vec2 gridUV = uv * 15.0;
    gridUV.x += time * 0.2;
    gridUV.y -= time * 0.15;

    // Grid lines
    vec2 grid = abs(fract(gridUV - 0.5) - 0.5);
    float gridLine = min(grid.x, grid.y);
    float verticalLines = smoothstep(0.0, 0.05, gridLine);

    // Balanced grid pattern
    float gridPattern = (1.0 - verticalLines) * 0.3;

    // Green-dominant cyber colors (not toyish)
    vec3 darkGreen = vec3(0.0, 0.5, 0.2);
    vec3 brightGreen = vec3(0.0, 0.8, 0.3);
    vec3 cyan = vec3(0.0, 0.6, 0.5);

    // Color shifts - green dominant
    float colorShift = sin(uv.x * 2.0 + time * 0.5) * 0.5 + 0.5;
    vec3 gridColor = mix(darkGreen, brightGreen, colorShift);
    gridColor = mix(gridColor, cyan, sin(uv.y * 1.5 - time * 0.3) * 0.3 + 0.2);

    // Base color
    vec3 color = gridColor * gridPattern;

    // Moving scan lines - green
    float movingLine1 = abs(sin(uv.y * 8.0 - time * 2.0));
    movingLine1 = smoothstep(0.93, 1.0, movingLine1);
    color += brightGreen * movingLine1 * 0.7;

    // Vertical pulses
    if (hash(vec2(floor(uv.x * 10.0), floor(time * 2.0))) > 0.98) {
        float pulse = abs(sin(uv.y * 20.0 + time * 5.0));
        color += brightGreen * smoothstep(0.9, 1.0, pulse) * 0.5;
    }

    // Hex codes - green
    vec2 hexGrid = uv * vec2(25.0, 15.0);
    vec2 hexCell = floor(hexGrid);

    if (hash(hexCell) > 0.93 && hash(hexCell + vec2(123.0, 456.0)) > 0.75) {
        float hexValue = hash(hexCell + floor(time * 0.5));
        float hexChar = hexDigit(fract(hexGrid), hexValue);
        color += brightGreen * hexChar * 0.6;
    }

    // Data streams - cyan accent
    float dataStream = step(0.993, hash(vec2(floor(uv.x * 40.0), floor(time * 3.0 - uv.y * 15.0))));
    color += cyan * dataStream * 0.5;

    // Glitch blocks - green
    float glitchBlock = step(0.99, hash(vec2(floor(uv.x * 8.0), floor(time * 2.0))));
    if (glitchBlock > 0.5) {
        vec3 glitchColor = brightGreen * hash(vec2(time * 2.0, uv.y)) * 1.2;
        color = mix(color, glitchColor, glitchBlock * 0.5);
    }

    // Scanlines
    float scanline = 0.9 + 0.1 * sin(gl_FragCoord.y * 2.5 - time * 12.0);
    color *= scanline;

    // Balanced vignette
    vec2 vignetteUV = gl_FragCoord.xy / resolution.xy - 0.5;
    float vignette = 1.0 - dot(vignetteUV, vignetteUV) * 0.8;
    vignette = pow(vignette, 2.0);
    color *= vignette;

    // Horizontal scan - green
    float horizontalScan = smoothstep(0.0, 0.005, abs(fract(uv.y - time * 0.15) - 0.5));
    color += darkGreen * (1.0 - horizontalScan) * 0.08;

    // Screen flicker
    float flicker = 0.97 + 0.03 * hash(vec2(floor(time * 50.0), 0.0));
    color *= flicker;

    // Noise
    color += vec3(hash(gl_FragCoord.xy + time * 0.1) * 0.015);

    // Balanced brightness (not too dark, not toyish)
    color *= 0.85;

    gl_FragColor = vec4(color, 1.0);
}
