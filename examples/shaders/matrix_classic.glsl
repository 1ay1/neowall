#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Simple hash function
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Generate ASCII-like characters (numbers, letters, symbols)
float asciiChar(vec2 uv, float n) {
    uv = fract(uv);

    // Create different character patterns based on n value
    float c = 0.0;
    vec2 block = floor(uv * 5.0);

    // Numbers 0-9 and letters patterns
    float index = floor(n * 36.0); // 0-35 range

    if (index < 1.0) {
        // '0'
        vec2 center = abs(uv - 0.5);
        c = step(max(center.x, center.y), 0.4) * (1.0 - step(max(center.x, center.y), 0.25));
    }
    else if (index < 2.0) {
        // '1'
        c = step(abs(uv.x - 0.5), 0.15);
    }
    else if (index < 3.0) {
        // '2'
        c = step(uv.y, 0.3) * step(0.3, uv.x);
        c += step(0.4, uv.y) * step(uv.y, 0.6);
        c += step(0.7, uv.y) * step(uv.x, 0.7);
    }
    else if (index < 4.0) {
        // '3'
        c = step(0.3, uv.x);
        c *= step(uv.y, 0.3) + step(0.4, uv.y) * step(uv.y, 0.6) + step(0.7, uv.y);
    }
    else if (index < 5.0) {
        // '4'
        c = step(uv.y, 0.6) * step(abs(uv.x - 0.25), 0.2);
        c += step(0.3, uv.y) * step(abs(uv.x - 0.65), 0.15);
    }
    else if (index < 6.0) {
        // '5'
        c = step(uv.y, 0.3);
        c += step(0.3, uv.y) * step(uv.y, 0.6) * step(0.3, uv.x);
        c += step(0.6, uv.y) * step(uv.x, 0.7);
    }
    else if (index < 7.0) {
        // '6'
        c = step(abs(uv.x - 0.35), 0.25);
        c += step(0.5, uv.y) * step(max(abs(uv.x - 0.5), abs(uv.y - 0.7)), 0.25);
    }
    else if (index < 8.0) {
        // '7'
        c = step(uv.y, 0.25);
        c += step(0.25, uv.y) * step(abs(uv.x - 0.5 + (uv.y - 0.5) * 0.3), 0.15);
    }
    else if (index < 9.0) {
        // '8'
        vec2 top = abs(uv - vec2(0.5, 0.3));
        vec2 bot = abs(uv - vec2(0.5, 0.7));
        c = step(max(top.x, top.y), 0.25) * (1.0 - step(max(top.x, top.y), 0.15));
        c += step(max(bot.x, bot.y), 0.25) * (1.0 - step(max(bot.x, bot.y), 0.15));
    }
    else if (index < 10.0) {
        // '9'
        vec2 top = abs(uv - vec2(0.5, 0.3));
        c = step(max(top.x, top.y), 0.25) * (1.0 - step(max(top.x, top.y), 0.15));
        c += step(0.5, uv.y) * step(abs(uv.x - 0.65), 0.15);
    }
    else if (index < 15.0) {
        // Simple vertical lines for 'I', 'l', '|'
        c = step(abs(uv.x - 0.5), 0.15);
    }
    else if (index < 20.0) {
        // Horizontal lines for '-', '_', '='
        c = step(abs(uv.y - 0.5), 0.15);
    }
    else if (index < 25.0) {
        // Diagonal for '/', '\', 'X'
        float d1 = abs(uv.x - uv.y);
        c = step(d1, 0.2);
    }
    else if (index < 30.0) {
        // Colon ':'
        c = step(length(uv - vec2(0.5, 0.3)), 0.15);
        c += step(length(uv - vec2(0.5, 0.7)), 0.15);
    }
    else {
        // Random patterns for other chars
        c = step(0.3, fract(uv.x * 5.0)) * step(fract(uv.x * 5.0), 0.6);
        c += step(0.4, fract(uv.y * 5.0)) * step(fract(uv.y * 5.0), 0.7);
        c = min(c, 1.0);
    }

    return c;
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Columns of characters
    vec2 charGrid = vec2(uv.x * resolution.x / 12.0, uv.y * resolution.y / 20.0);
    vec2 charPos = floor(charGrid);
    vec2 charUV = fract(charGrid);

    // Each column has random speed
    float colSeed = hash(vec2(charPos.x, 0.0));
    float speed = 0.3 + colSeed * 0.7;
    float offset = hash(vec2(charPos.x, 123.0)) * 100.0;

    // Falling position
    float fall = fract(time * speed + offset);

    // Current row position (0-1)
    float rowPos = charPos.y / (resolution.y / 20.0);

    // Distance from head
    float dist = rowPos - fall;
    if (dist < -0.5) dist += 1.0;

    // Trail length
    float trailLen = 0.4 + colSeed * 0.3;

    float brightness = 0.0;

    // Bright white head
    if (dist > -0.02 && dist < 0.02) {
        brightness = 1.5;
    }
    // Green trail
    else if (dist > -trailLen && dist < 0.0) {
        brightness = pow((trailLen + dist) / trailLen, 2.0);
    }

    // Character selection
    float charSeed = hash(vec2(charPos.x, charPos.y + floor(time * 4.0)));
    float charValue = asciiChar(charUV, charSeed);

    // Characters flicker occasionally
    if (hash(vec2(charPos.x, charPos.y + floor(time * 10.0))) > 0.95) {
        charSeed = hash(vec2(charPos.x, charPos.y + time * 50.0));
        charValue = asciiChar(charUV, charSeed);
    }

    float finalChar = charValue * brightness;

    // Color
    vec3 color = vec3(0.0);

    if (finalChar > 0.01) {
        // White head
        if (dist > -0.02 && dist < 0.02) {
            color = vec3(0.8, 1.0, 0.9) * finalChar;
        }
        // Green trail
        else {
            color = vec3(0.0, 0.9, 0.1) * finalChar;
        }
    }

    // Scanlines
    float scanline = 0.95 + 0.05 * sin(gl_FragCoord.y * 2.0);
    color *= scanline;

    gl_FragColor = vec4(color, 1.0);
}
