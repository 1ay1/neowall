#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function for pseudo-random numbers
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Better hash for characters
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Generate pseudo-ASCII character pattern
float character(vec2 uv, float seed) {
    uv = fract(uv);

    // Simple character patterns using grid
    float pattern = 0.0;
    vec2 grid = floor(uv * 4.0);

    // Different patterns for different character types
    float charType = floor(hash(vec2(seed)) * 16.0);

    if (charType < 4.0) {
        // Vertical bars
        pattern = step(0.3, fract(uv.x * 4.0)) * step(fract(uv.x * 4.0), 0.7);
    } else if (charType < 8.0) {
        // Horizontal bars
        pattern = step(0.3, fract(uv.y * 4.0)) * step(fract(uv.y * 4.0), 0.7);
    } else if (charType < 12.0) {
        // Diagonal
        float diag = fract((uv.x + uv.y) * 4.0);
        pattern = step(0.4, diag) * step(diag, 0.6);
    } else {
        // Dots and patterns
        vec2 center = fract(uv * 4.0) - 0.5;
        pattern = 1.0 - smoothstep(0.1, 0.3, length(center));
    }

    return pattern;
}

// Terminal text character
float terminalChar(vec2 coord, float line, float offset, float seed) {
    vec2 cellCoord = fract(coord);
    float charSeed = hash(vec2(line, floor(coord.x) + offset)) * 1000.0;
    return character(cellCoord, charSeed);
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Terminal aspect ratio adjustment
    vec2 termUV = uv;
    termUV.x *= resolution.x / resolution.y;

    // Scale for character grid
    vec2 charCoord = termUV * vec2(80.0, 40.0);
    vec2 charID = floor(charCoord);

    // Scrolling effect - text moves up
    float scroll = time * 5.0;
    float line = charID.y + scroll;

    // Get character at this position
    float charPattern = terminalChar(charCoord + vec2(0.0, scroll), line, 0.0, time);

    // Typing effect on the last line
    float currentLine = 40.0;
    float typingPos = fract(time * 10.0) * 80.0;

    // Cursor blinking
    float cursorBlink = step(0.5, fract(time * 2.0));
    float cursor = 0.0;
    if (charID.y > currentLine - scroll - 1.0 && charID.y < currentLine - scroll + 1.0) {
        if (charID.x > typingPos - 1.0 && charID.x < typingPos + 1.0) {
            cursor = cursorBlink;
        }
    }

    // Text brightness based on age (newer = brighter)
    float age = (currentLine - (charID.y + scroll)) / 40.0;
    age = clamp(age, 0.0, 1.0);
    float brightness = 1.0 - age * 0.7;

    // Random bright "active" lines
    float activeLine = step(0.98, hash(vec2(line, floor(time))));
    brightness += activeLine * 0.5;

    // Combine character and cursor
    float finalChar = max(charPattern * brightness, cursor);

    // Terminal green color with variations
    vec3 color = vec3(0.0);

    if (finalChar > 0.0) {
        // Main green
        color = vec3(0.0, 0.9, 0.1) * finalChar;

        // Add cyan tint to cursor
        if (cursor > 0.0) {
            color = mix(color, vec3(0.0, 1.0, 0.8), 0.5);
        }

        // Add slight color variation to active lines
        if (activeLine > 0.0) {
            color.b += 0.2;
        }
    }

    // Background ambient glow from text
    float glow = 0.0;
    for (float dy = -2.0; dy <= 2.0; dy += 1.0) {
        for (float dx = -2.0; dx <= 2.0; dx += 1.0) {
            if (dx == 0.0 && dy == 0.0) continue;
            vec2 sampleCoord = charCoord + vec2(dx, dy);
            vec2 sampleID = floor(sampleCoord);
            float sampleLine = sampleID.y + scroll;
            float sampleChar = terminalChar(sampleCoord + vec2(0.0, scroll), sampleLine, 0.0, time);
            float dist = length(vec2(dx, dy));
            glow += sampleChar / (dist * dist) * 0.1;
        }
    }
    color += vec3(0.0, glow * 0.3, glow * 0.1);

    // CRT scanline effect
    float scanline = 0.95 + 0.05 * sin(gl_FragCoord.y * 2.0 - time * 20.0);
    color *= scanline;

    // Horizontal scanline sweep
    float sweepLine = smoothstep(0.0, 0.02, abs(fract(uv.y - time * 0.3) - 0.5)) * 0.3;
    color += vec3(0.0, sweepLine, sweepLine * 0.5);

    // Screen curvature effect
    vec2 curveUV = uv * 2.0 - 1.0;
    float curvature = pow(length(curveUV), 1.5) * 0.1;
    color *= 1.0 - curvature;

    // Vignette
    float vignette = 1.0 - length(uv - 0.5) * 0.8;
    vignette = smoothstep(0.3, 0.8, vignette);
    color *= vignette;

    // Screen flicker
    float flicker = 0.98 + 0.02 * hash(vec2(time * 10.0, 0.0));
    color *= flicker;

    // Random glitch effect
    if (hash(vec2(floor(time * 5.0), charID.y)) > 0.97) {
        color.r += hash(vec2(time, charID.x)) * 0.3;
        color.g *= 1.5;
    }

    // Noise grain
    float noise = hash(gl_FragCoord.xy + time * 0.1) * 0.05;
    color += vec3(noise);

    // Phosphor glow effect
    color = pow(color, vec3(0.8));

    gl_FragColor = vec4(color, 1.0);
}
