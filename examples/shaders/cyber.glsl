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

// Hexagonal grid pattern
float hexGrid(vec2 p) {
    vec2 h = vec2(1.0, 1.73205);
    vec2 a = mod(p, h) - h * 0.5;
    vec2 b = mod(p - h * 0.5, h) - h * 0.5;
    return min(dot(a, a), dot(b, b));
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

    // Simple segment display
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

    // Much darker, sparser grid
    vec2 gridUV = uv * 15.0;
    gridUV.x += time * 0.2;
    gridUV.y -= time * 0.15;

    // Thin grid lines - mostly black space (manual grid without fwidth)
    vec2 grid = abs(fract(gridUV - 0.5) - 0.5);
    float gridLine = min(grid.x, grid.y);
    float verticalLines = smoothstep(0.0, 0.05, gridLine);

    // More visible grid pattern
    float gridPattern = (1.0 - verticalLines) * 0.5;

    // Brighter cyber colors
    vec3 cyan = vec3(0.0, 0.8, 1.0);
    vec3 green = vec3(0.0, 1.0, 0.5);
    vec3 blue = vec3(0.2, 0.5, 1.0);

    // Color shifts based on position
    float colorShift = sin(uv.x * 2.0 + time * 0.5) * 0.5 + 0.5;
    vec3 gridColor = mix(cyan, green, colorShift);
    gridColor = mix(gridColor, blue, sin(uv.y * 1.5 - time * 0.3) * 0.5 + 0.5);

    // Dark grid base
    vec3 color = gridColor * gridPattern;

    // Brighter moving scan lines
    float movingLine1 = abs(sin(uv.y * 8.0 - time * 2.0));
    movingLine1 = smoothstep(0.92, 1.0, movingLine1);
    color += cyan * movingLine1 * 1.2;

    // Rare vertical pulses
    if (hash(vec2(floor(uv.x * 10.0), floor(time * 2.0))) > 0.98) {
        float pulse = abs(sin(uv.y * 20.0 + time * 5.0));
        color += green * smoothstep(0.88, 1.0, pulse) * 0.8;
    }

    // Memory address display (hex codes)
    vec2 hexGrid = uv * vec2(25.0, 15.0);
    vec2 hexCell = floor(hexGrid);

    // Only show hex on certain cells (sparse)
    if (hash(hexCell) > 0.92 && hash(hexCell + vec2(123.0, 456.0)) > 0.7) {
        float hexValue = hash(hexCell + floor(time * 0.5));
        float hexChar = hexDigit(fract(hexGrid), hexValue);
        color += green * hexChar * 0.9;
    }

    // Binary data streams (very rare)
    float dataStream = step(0.992, hash(vec2(floor(uv.x * 40.0), floor(time * 3.0 - uv.y * 15.0))));
    color += vec3(0.0, 0.9, 0.7) * dataStream * 0.7;

    // Glitch blocks - rare but visible
    float glitchBlock = step(0.99, hash(vec2(floor(uv.x * 8.0), floor(time * 2.0))));
    if (glitchBlock > 0.5) {
        vec3 glitchColor = vec3(
            hash(vec2(time * 2.0, uv.y)) * 0.8,
            hash(vec2(time * 2.0 + 1.0, uv.y)) * 1.0,
            hash(vec2(time * 2.0 + 2.0, uv.y)) * 0.9
        );
        color = mix(color, glitchColor, glitchBlock * 0.6);
    }

    // Scan lines - subtle
    float scanline = 0.92 + 0.08 * sin(gl_FragCoord.y * 2.5 - time * 12.0);
    color *= scanline;

    // Lighter vignette
    vec2 vignetteUV = gl_FragCoord.xy / resolution.xy - 0.5;
    float vignette = 1.0 - dot(vignetteUV, vignetteUV) * 0.6;
    vignette = pow(vignette, 1.5);
    color *= vignette;

    // Horizontal scan with technical data
    float horizontalScan = smoothstep(0.0, 0.005, abs(fract(uv.y - time * 0.15) - 0.5));
    color += vec3(0.0, 0.6, 0.4) * (1.0 - horizontalScan) * 0.15;

    // Screen flicker
    float flicker = 0.98 + 0.02 * hash(vec2(floor(time * 50.0), 0.0));
    color *= flicker;

    // Minimal noise
    color += vec3(hash(gl_FragCoord.xy + time * 0.1) * 0.02);

    // Brighter overall
    color *= 1.3;

    // Better contrast
    color = pow(color, vec3(0.9));

    gl_FragColor = vec4(color, 1.0);
}
