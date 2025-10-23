#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Hash function for pseudo-random numbers
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Better hash for 3D
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Generate hex digit patterns (0-9, A-F)
float hexChar(vec2 uv, float value) {
    uv = fract(uv);
    float pattern = 0.0;

    // Convert to segments based on hex value
    float digit = floor(value * 16.0);
    vec2 grid = floor(uv * 3.0);

    // 7-segment style hex digits
    if (digit < 1.0) { // 0
        pattern = step(abs(uv.x - 0.5), 0.3) * step(abs(uv.y - 0.5), 0.4);
        pattern *= 1.0 - step(abs(uv.x - 0.5), 0.15) * step(abs(uv.y - 0.5), 0.25);
    } else if (digit < 2.0) { // 1
        pattern = step(abs(uv.x - 0.6), 0.15);
    } else if (digit < 10.0) { // 2-9
        // Simplified patterns for other digits
        pattern = step(0.3, fract(uv.x * 3.0)) * step(fract(uv.x * 3.0), 0.7);
        pattern += step(0.3, fract(uv.y * 3.0)) * step(fract(uv.y * 3.0), 0.7);
        pattern = min(pattern, 1.0);
    } else { // A-F
        // Letter-like patterns
        vec2 center = abs(uv - 0.5);
        pattern = step(center.x, 0.3) * step(center.y, 0.4);
        pattern *= step(0.1, center.x + center.y);
    }

    return pattern;
}

// System prompt characters
float sysChar(vec2 uv, float seed) {
    uv = fract(uv);
    vec2 grid = floor(uv * 4.0);
    float r = hash(grid + seed);

    float pattern = 0.0;
    float charType = floor(r * 12.0);

    if (charType < 2.0) {
        // '>' prompt
        float slope = uv.y - uv.x * 0.5 - 0.25;
        pattern = step(abs(slope), 0.15);
    } else if (charType < 4.0) {
        // '[' or ']'
        pattern = step(abs(uv.x - 0.2), 0.1);
        pattern += step(abs(uv.y - 0.2), 0.1) * step(uv.x, 0.3);
        pattern += step(abs(uv.y - 0.8), 0.1) * step(uv.x, 0.3);
        pattern = min(pattern, 1.0);
    } else if (charType < 6.0) {
        // '|' or '/'
        pattern = step(abs(uv.x - 0.5), 0.1);
    } else if (charType < 8.0) {
        // '@' or '#'
        vec2 center = abs(uv - 0.5);
        pattern = 1.0 - smoothstep(0.15, 0.25, length(center));
        pattern += step(center.x, 0.35) * step(center.y, 0.35);
        pattern = min(pattern, 1.0);
    } else {
        // Random patterns
        pattern = step(0.4, fract(uv.x * 4.0)) * step(fract(uv.x * 4.0), 0.6);
        pattern += step(0.4, fract(uv.y * 4.0)) * step(fract(uv.y * 4.0), 0.6);
        pattern = min(pattern, 1.0);
    }

    return pattern * step(0.3, r);
}

// Memory address display (0x...)
float memoryAddress(vec2 uv, vec2 cellID, float offset) {
    float isAddressLine = step(0.97, hash(vec2(cellID.y + offset, 123.0)));
    if (isAddressLine < 0.5) return 0.0;

    // Show "0x" prefix
    if (cellID.x < 2.0) {
        if (cellID.x < 1.0) {
            // '0'
            vec2 center = abs(fract(uv) - 0.5);
            return smoothstep(0.35, 0.3, max(center.x, center.y)) -
                   smoothstep(0.25, 0.2, max(center.x, center.y));
        } else {
            // 'x'
            vec2 uv2 = fract(uv);
            float diag1 = abs(uv2.x - uv2.y);
            float diag2 = abs(uv2.x + uv2.y - 1.0);
            return step(diag1, 0.2) + step(diag2, 0.2);
        }
    } else if (cellID.x < 10.0) {
        // Hex digits
        float hexValue = hash(vec2(cellID.x, cellID.y + time * 0.1));
        return hexChar(uv, hexValue);
    }

    return 0.0;
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Terminal grid - very dense
    vec2 charCoord = uv * vec2(resolution.x / 8.0, resolution.y / 14.0);
    vec2 charID = floor(charCoord);
    vec2 charUV = fract(charCoord);

    // Much slower scroll
    float scroll = time * 2.0;
    float line = charID.y + scroll;

    // Different line types
    float lineType = hash(vec2(0.0, floor(line)));
    float charPattern = 0.0;

    if (lineType > 0.85) {
        // Memory addresses (0x...)
        charPattern = memoryAddress(charUV, charID, scroll);
    } else if (lineType > 0.75) {
        // System messages with brackets
        charPattern = sysChar(charUV, line * 100.0 + charID.x);
    } else if (lineType > 0.4) {
        // Regular hex/code output
        float charSeed = hash(vec2(charID.x, line)) * 1000.0;
        charPattern = hexChar(charUV, hash(vec2(charSeed, time * 0.1)));
    }
    // else: empty line (black)

    // Cursor on active line
    float currentLine = (resolution.y / 14.0) - 5.0;
    float typingPos = fract(time * 5.0) * (resolution.x / 8.0);
    float cursorBlink = step(0.5, fract(time * 1.5));
    float cursor = 0.0;

    if (charID.y > currentLine - scroll - 1.0 && charID.y < currentLine - scroll + 1.0) {
        if (charID.x > typingPos - 1.0 && charID.x < typingPos + 1.0) {
            cursor = cursorBlink * 0.8;
        }
    }

    // Age-based brightness (newer = brighter, but still dark)
    float age = (currentLine - (charID.y + scroll)) / 40.0;
    age = clamp(age, 0.0, 1.0);
    float brightness = (1.0 - age * 0.85) * 0.4; // Much darker

    // Rare bright lines (system alerts)
    float activeLine = step(0.985, hash(vec2(line, floor(time * 0.5))));
    brightness += activeLine * 0.3;

    // Combine character and cursor
    float finalChar = max(charPattern * brightness, cursor);

    // Very dark phosphor green
    vec3 color = vec3(0.0);

    if (finalChar > 0.005) {
        // Main dark green
        color = vec3(0.0, 0.35, 0.05) * finalChar;

        // Cursor is slightly brighter cyan-green
        if (cursor > 0.0) {
            color = mix(color, vec3(0.0, 0.5, 0.2), 0.4);
        }

        // Active lines get slight cyan tint
        if (activeLine > 0.0) {
            color.b += 0.08;
            color.g += 0.1;
        }
    }

    // Minimal background glow - very subtle
    float glow = 0.0;
    for (float dy = -1.0; dy <= 1.0; dy += 1.0) {
        for (float dx = -1.0; dx <= 1.0; dx += 1.0) {
            if (dx == 0.0 && dy == 0.0) continue;
            vec2 sampleCoord = charCoord + vec2(dx, dy);
            vec2 sampleID = floor(sampleCoord);
            float sampleLine = sampleID.y + scroll;
            float sampleType = hash(vec2(0.0, floor(sampleLine)));
            float sampleChar = 0.0;
            if (sampleType > 0.4) {
                sampleChar = hexChar(fract(sampleCoord), hash(vec2(sampleID.x, sampleLine)) * 1000.0);
            }
            float dist = length(vec2(dx, dy));
            glow += sampleChar / (dist * dist) * 0.02;
        }
    }
    color += vec3(0.0, glow * 0.1, glow * 0.05);

    // Strong CRT scanline
    float scanline = 0.75 + 0.25 * sin(gl_FragCoord.y * 3.0 - time * 30.0);
    color *= scanline;

    // Rare horizontal sweep (diagnostic scan)
    float sweepLine = smoothstep(0.0, 0.003, abs(fract(uv.y - time * 0.1) - 0.5));
    color += vec3(0.0, sweepLine, sweepLine * 0.3) * 0.03;

    // Stronger vignette - very dark edges
    float vignette = 1.0 - length(uv - 0.5) * 1.2;
    vignette = pow(vignette, 3.0);
    color *= vignette;

    // Screen flicker
    float flicker = 0.92 + 0.08 * hash(vec2(floor(time * 60.0), 0.0));
    color *= flicker;

    // Rare glitch lines
    if (hash(vec2(floor(time * 3.0), charID.y)) > 0.99) {
        color.r += hash(vec2(time * 5.0, charID.x)) * 0.15;
        color.g *= 1.3;
    }

    // Very minimal noise
    color += vec3(hash(gl_FragCoord.xy + time * 0.05) * 0.01);

    // Phosphor persistence (ghosting)
    color = pow(color, vec3(0.9));

    // Overall darkness - mostly black screen
    color *= 0.55;

    gl_FragColor = vec4(color, 1.0);
}
