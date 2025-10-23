#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Enhanced pseudo-random function
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Generate realistic hex/binary character patterns
float character(vec2 uv, float seed) {
    uv = fract(uv);
    vec2 grid = floor(uv * 5.0);
    float r = hash(grid + seed);

    float pattern = 0.0;
    float charType = floor(r * 10.0);

    // Hex characters (0-9, A-F)
    if (charType < 3.0) {
        // Vertical bars for '1', 'I', '|'
        pattern = step(0.35, fract(uv.x * 5.0)) * step(fract(uv.x * 5.0), 0.65);
    } else if (charType < 5.0) {
        // Horizontal bars for '-', '_', '='
        pattern = step(0.35, fract(uv.y * 5.0)) * step(fract(uv.y * 5.0), 0.65);
    } else if (charType < 7.0) {
        // Diagonal for '/', '\', 'X'
        float diag1 = abs(fract(uv.x * 5.0) - fract(uv.y * 5.0));
        pattern = step(diag1, 0.3);
    } else if (charType < 8.5) {
        // Blocks for '0', 'O', 'D'
        vec2 center = abs(fract(uv * 5.0) - 0.5);
        float rect = max(center.x, center.y);
        pattern = smoothstep(0.4, 0.3, rect) - smoothstep(0.3, 0.2, rect);
    } else {
        // Random dots for '.', '*', ','
        vec2 center = fract(uv * 5.0) - 0.5;
        pattern = 1.0 - smoothstep(0.05, 0.25, length(center));
    }

    return pattern;
}

// Binary digit pattern (0 or 1)
float binaryChar(vec2 uv, float seed) {
    uv = fract(uv);
    float isBinary = step(0.5, hash(vec2(seed, 123.456)));

    if (isBinary < 0.5) {
        // Draw '0'
        vec2 center = abs(fract(uv * 4.0) - 0.5);
        float outer = max(center.x, center.y);
        float inner = max(center.x * 0.6, center.y * 0.6);
        return smoothstep(0.45, 0.35, outer) - smoothstep(0.35, 0.25, inner);
    } else {
        // Draw '1'
        return step(0.4, fract(uv.x * 4.0)) * step(fract(uv.x * 4.0), 0.6);
    }
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;
    uv.x *= resolution.x / resolution.y;

    // Darker, more columns for dense matrix effect
    vec2 grid = uv * vec2(60.0, 45.0);
    vec2 gridCell = floor(grid);
    vec2 gridUV = fract(grid);

    // Column seed
    float columnSeed = hash(vec2(gridCell.x, 0.0));

    // Different falling speeds per column
    float speed = 0.8 + columnSeed * 1.5;
    float fall = fract(time * speed * 0.25 + columnSeed * 10.0);

    // Current position in column
    float cellY = gridCell.y / 45.0;
    float distanceFromHead = cellY - fall;
    if (distanceFromHead < -0.5) distanceFromHead += 1.0;

    // Much shorter, darker trails
    float trailLength = 0.15 + columnSeed * 0.1;
    float brightness = 0.0;

    // Bright head
    if (distanceFromHead > -0.02 && distanceFromHead < 0.02) {
        brightness = 1.2;
    } else if (distanceFromHead > -trailLength && distanceFromHead < 0.0) {
        // Much faster falloff for darker trail
        brightness = pow((trailLength + distanceFromHead) / trailLength, 3.5);
        brightness *= 0.6; // Darker trails
    }

    // Character selection - mix binary and hex
    float useBinary = step(0.6, hash(vec2(gridCell.x + 100.0, 0.0)));
    float charSeed = columnSeed + floor(time * 8.0 + gridCell.y);
    float charPattern;

    if (useBinary > 0.5) {
        charPattern = binaryChar(gridUV, charSeed);
    } else {
        charPattern = character(gridUV, charSeed);
    }

    // Less frequent glitches, but brighter
    float glitch = 0.0;
    if (hash(vec2(gridCell.x, floor(time * 3.0))) > 0.985) {
        glitch = hash(gridCell + time) * 0.8;
    }

    float charBrightness = brightness * charPattern + glitch;

    // Much darker, more authentic Matrix green
    vec3 color = vec3(0.0);

    if (charBrightness > 0.01) {
        // Head is brighter cyan-white
        if (distanceFromHead > -0.02 && distanceFromHead < 0.02) {
            color = vec3(0.6, 1.0, 0.7) * charBrightness;
        } else {
            // Dark phosphor green - much less bright
            color = vec3(0.0, 0.5 + hash(gridCell) * 0.15, 0.0) * charBrightness;

            // Very rare blue tint for depth
            if (hash(gridCell + vec2(time * 0.05, 0.0)) > 0.95) {
                color.b += 0.1 * charBrightness;
            }
        }
    }

    // Minimal ambient glow - much darker background
    float ambientGlow = 0.0;
    for (float i = 0.0; i < 5.0; i++) {
        float checkFall = fract(time * (0.8 + hash(vec2(gridCell.x, i)) * 1.5) * 0.25
                                + hash(vec2(gridCell.x, i)) * 10.0);
        float checkDist = abs(cellY - checkFall);
        if (checkDist < 0.2) {
            ambientGlow += (0.2 - checkDist) * 0.015; // Much less glow
        }
    }
    color += vec3(0.0, ambientGlow * 0.15, 0.0);

    // Stronger scanline for CRT effect
    float scanline = 0.9 + 0.1 * sin(gl_FragCoord.y * 2.0 - time * 15.0);
    color *= scanline;

    // Subtle horizontal scan
    float horizontalScan = smoothstep(0.0, 0.01, abs(fract(uv.y - time * 0.2) - 0.5));
    color += vec3(0.0, horizontalScan * 0.03, 0.0);

    // Stronger vignette for darker edges
    vec2 vignetteUV = gl_FragCoord.xy / resolution.xy;
    float vignette = 1.0 - length(vignetteUV - 0.5) * 0.9;
    vignette = pow(vignette, 2.0);
    color *= vignette;

    // Minimal noise - darker overall
    color += vec3(hash(gl_FragCoord.xy + time * 0.1) * 0.01);

    // Screen flicker
    float flicker = 0.97 + 0.03 * hash(vec2(floor(time * 60.0), 0.0));
    color *= flicker;

    // Darken everything significantly
    color *= 0.7;

    gl_FragColor = vec4(color, 1.0);
}
