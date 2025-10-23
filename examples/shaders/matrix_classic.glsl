#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Enhanced hash function for better randomness
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float hash3(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

// Japanese Katakana-like characters using 5x7 grid
float katakanaChar(vec2 uv, float n) {
    uv = fract(uv);
    vec2 grid = floor(uv * vec2(5.0, 7.0));

    float charIndex = floor(n * 50.0);
    float px = 0.0;

    // Create various Katakana-like patterns
    if (charIndex < 5.0) {
        // ア-like vertical with diagonal
        px = step(grid.x, 1.0) * step(1.0, grid.y) * step(grid.y, 5.0);
        px += step(abs(grid.x - grid.y), 0.5) * step(2.0, grid.y);
    }
    else if (charIndex < 10.0) {
        // カ-like character
        px = step(grid.y, 1.0);
        px += step(abs(grid.x - 2.0), 0.5) * step(1.0, grid.y);
        px += step(3.0, grid.y) * step(grid.y, 4.0) * step(abs(grid.x - 2.5), 1.5);
    }
    else if (charIndex < 15.0) {
        // サ-like character
        px = step(grid.y, 1.0);
        px += step(2.0, grid.y) * step(grid.y, 3.0);
        px += step(4.0, grid.y) * step(grid.y, 5.0);
    }
    else if (charIndex < 20.0) {
        // タ-like character
        px = step(grid.y, 1.0);
        px += step(abs(grid.x - 2.0), 0.5) * step(2.0, grid.y);
    }
    else if (charIndex < 25.0) {
        // ナ-like character
        px = step(abs(grid.x - 0.5), 0.5) * step(1.0, grid.y) * step(grid.y, 5.0);
        px += step(2.0, grid.y) * step(grid.y, 3.0);
        px += step(abs(grid.x - 4.0 + grid.y), 1.0) * step(3.0, grid.y);
    }
    else if (charIndex < 30.0) {
        // ハ-like character
        px = step(abs(grid.x - 1.5 + grid.y * 0.3), 0.5);
        px += step(abs(grid.x - 3.5 - grid.y * 0.3), 0.5);
    }
    else if (charIndex < 35.0) {
        // マ-like character
        px = step(grid.y, 1.0);
        px += step(abs(grid.x - 1.0), 0.5) * step(1.0, grid.y) * step(grid.y, 4.0);
        px += step(abs(grid.x - 3.0), 0.5) * step(1.0, grid.y) * step(grid.y, 4.0);
        px += step(3.0, grid.y) * step(grid.y, 4.0);
    }
    else if (charIndex < 40.0) {
        // ラ-like character
        px = step(grid.y, 1.0);
        px += step(abs(grid.x - 3.5), 0.5) * step(1.0, grid.y);
    }
    else if (charIndex < 45.0) {
        // ワ-like character
        px = step(grid.y, 1.0);
        px += step(abs(grid.x - 1.0), 0.5) * step(2.0, grid.y) * step(grid.y, 5.0);
        px += step(abs(grid.x - 3.0), 0.5) * step(2.0, grid.y) * step(grid.y, 5.0);
    }
    else {
        // Numbers and symbols
        px = step(abs(grid.x - 2.0), 1.0) * step(abs(grid.y - 3.0), 2.0);
        px *= step(0.5, hash(vec2(grid.x, grid.y + charIndex)));
    }

    return px;
}

// Add glow effect to bright characters
vec3 addGlow(vec3 color, float intensity) {
    float glow = pow(intensity, 2.0) * 0.3;
    return color + vec3(0.0, glow * 0.8, glow * 0.3);
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Character grid - smaller cells for more authentic look
    float cellWidth = 10.0;
    float cellHeight = 18.0;
    vec2 charGrid = vec2(uv.x * resolution.x / cellWidth, uv.y * resolution.y / cellHeight);
    vec2 charPos = floor(charGrid);
    vec2 charUV = fract(charGrid);

    // Each column has its own properties
    float colSeed = hash(vec2(charPos.x, 0.0));

    // Asynchronous scrolling - different speeds per column (cmatrix -a style)
    float speed = 0.15 + colSeed * 0.85;  // Wider speed variation
    float offset = hash(vec2(charPos.x, 456.0)) * 1000.0;

    // Random trail length per column (0.2 to 0.7 of screen height)
    float trailLen = 0.2 + hash(vec2(charPos.x, 789.0)) * 0.5;

    // Some columns pause briefly before continuing
    float pauseFactor = 1.0;
    float pausePhase = hash(vec2(charPos.x, 321.0)) * 10.0;
    if (mod(time + pausePhase, 8.0) < 0.5) {
        pauseFactor = 0.0;
    }

    // Falling position with pause effect
    float fall = fract((time * speed * pauseFactor + offset) * 0.1);

    // Current row position (0-1)
    float rowPos = charPos.y / (resolution.y / cellHeight);

    // Distance from head
    float dist = rowPos - fall;
    if (dist < -0.5) dist += 1.0;

    // Calculate brightness
    float brightness = 0.0;

    // Bright white/cyan head (very bright like cmatrix)
    if (dist > -0.015 && dist < 0.015) {
        brightness = 2.0;  // Super bright head
    }
    // Green trail with exponential falloff
    else if (dist > -trailLen && dist < 0.0) {
        float fadePos = (trailLen + dist) / trailLen;
        brightness = pow(fadePos, 1.5);  // Faster falloff for more dramatic effect
    }

    // Character selection - changes over time
    float charTime = floor(time * 8.0);
    float charSeed = hash3(vec3(charPos.x, charPos.y, charTime * 0.1));

    // Characters change randomly - some fast, some slow
    float changeSpeed = hash(vec2(charPos.x, charPos.y * 0.1));
    if (changeSpeed > 0.7) {
        // Fast changing characters (flicker effect)
        charSeed = hash3(vec3(charPos.x, charPos.y, time * 15.0));
    }

    float charValue = katakanaChar(charUV, charSeed);

    // Random character glitches
    if (hash3(vec3(charPos.x, charPos.y, floor(time * 20.0))) > 0.97) {
        charSeed = hash3(vec3(charPos.x, charPos.y, time * 50.0));
        charValue = katakanaChar(charUV, charSeed);
        brightness *= 1.5;  // Brighter when glitching
    }

    float finalChar = charValue * brightness;

    // Color scheme
    vec3 color = vec3(0.0);

    if (finalChar > 0.01) {
        // Bright cyan-white head (authentic Matrix look)
        if (dist > -0.015 && dist < 0.015) {
            color = vec3(0.7, 1.0, 0.9) * finalChar;
            color = addGlow(color, finalChar);
        }
        // Bright green for characters just behind head
        else if (dist > -0.08 && dist < 0.0) {
            color = vec3(0.3, 1.0, 0.4) * finalChar;
            color = addGlow(color, finalChar * 0.7);
        }
        // Medium green trail
        else if (dist > -trailLen * 0.5 && dist < -0.08) {
            color = vec3(0.1, 0.9, 0.2) * finalChar;
        }
        // Dark green fade-out
        else {
            color = vec3(0.0, 0.7, 0.1) * finalChar;
        }
    }

    // Subtle CRT scanline effect
    float scanline = 0.92 + 0.08 * sin(gl_FragCoord.y * 3.14159);
    color *= scanline;

    // Subtle vignette for depth
    vec2 vignetteUV = uv * 2.0 - 1.0;
    float vignette = 1.0 - dot(vignetteUV, vignetteUV) * 0.15;
    color *= vignette;

    // Very subtle random noise for texture
    float noise = hash(gl_FragCoord.xy + time * 0.1) * 0.015;
    color += noise;

    gl_FragColor = vec4(color, 1.0);
}
