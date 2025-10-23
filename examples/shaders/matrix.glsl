#version 100
precision highp float;

uniform float time;
uniform vec2 resolution;

// Enhanced pseudo-random function
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

// Random character-like pattern
float character(vec2 uv, float seed) {
    uv = fract(uv);
    float pattern = 0.0;

    // Create pseudo-characters using grid patterns
    vec2 grid = floor(uv * 5.0);
    float r = random(grid + seed);

    // Horizontal and vertical lines for character effect
    if (r > 0.5) {
        pattern = step(0.4, fract(uv.x * 5.0)) * step(fract(uv.x * 5.0), 0.6);
    } else {
        pattern = step(0.4, fract(uv.y * 5.0)) * step(fract(uv.y * 5.0), 0.6);
    }

    // Add some random dots
    if (random(grid + seed + 1.0) > 0.7) {
        vec2 center = fract(uv * 5.0) - 0.5;
        pattern = max(pattern, 1.0 - smoothstep(0.1, 0.3, length(center)));
    }

    return pattern;
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Adjust aspect ratio
    uv.x *= resolution.x / resolution.y;

    // Create multiple columns with different speeds
    vec2 grid = uv * vec2(40.0, 30.0);
    vec2 gridCell = floor(grid);
    vec2 gridUV = fract(grid);

    // Random seed for this column
    float columnSeed = random(vec2(gridCell.x, 0.0));

    // Falling speed variation per column
    float speed = 1.5 + columnSeed * 2.0;
    float fall = fract(time * speed * 0.3 + columnSeed * 10.0);

    // Current cell position in the fall
    float cellY = gridCell.y / 30.0;

    // Calculate if this cell is in the "rain" trail
    float trailLength = 0.4 + columnSeed * 0.3;
    float distanceFromHead = cellY - fall;

    // Wrap around for continuous fall
    if (distanceFromHead < -0.5) distanceFromHead += 1.0;

    // Brightness based on position in trail
    float brightness = 0.0;

    // Bright head
    if (distanceFromHead > -0.03 && distanceFromHead < 0.03) {
        brightness = 1.0;
        // Add white flash at the head
    } else if (distanceFromHead > -trailLength && distanceFromHead < 0.0) {
        // Fading trail
        brightness = (trailLength + distanceFromHead) / trailLength;
        brightness = pow(brightness, 2.0); // Non-linear falloff for better look
    }

    // Add character pattern
    float charPattern = character(gridUV, columnSeed + floor(time * 10.0 + gridCell.y));

    // Glitch effect - random bright flashes
    float glitch = 0.0;
    if (random(vec2(gridCell.x, floor(time * 5.0))) > 0.95) {
        glitch = random(gridCell + time) * 0.5;
    }

    // Character brightness with glitches
    float charBrightness = brightness * charPattern + glitch;

    // Matrix green color with variations
    vec3 color = vec3(0.0);

    if (charBrightness > 0.0) {
        // Bright head is more cyan/white
        if (distanceFromHead > -0.03 && distanceFromHead < 0.03) {
            color = vec3(0.8, 1.0, 0.9) * charBrightness;
        } else {
            // Classic Matrix green
            color = vec3(0.0, 0.8 + random(gridCell) * 0.2, 0.0) * charBrightness;
        }

        // Add slight blue tint to some characters
        if (random(gridCell + vec2(time * 0.1, 0.0)) > 0.8) {
            color.b += 0.2 * charBrightness;
        }
    }

    // Background ambient glow
    float ambientGlow = 0.0;
    for (float i = 0.0; i < 10.0; i++) {
        float checkFall = fract(time * (1.5 + random(vec2(gridCell.x, i)) * 2.0) * 0.3
                                + random(vec2(gridCell.x, i)) * 10.0);
        float checkDist = abs(cellY - checkFall);
        if (checkDist < 0.4) {
            ambientGlow += (0.4 - checkDist) * 0.05;
        }
    }
    color += vec3(0.0, ambientGlow * 0.3, 0.0);

    // Scanline effect for retro CRT feel
    float scanline = sin(gl_FragCoord.y * 1.5 + time * 10.0) * 0.05 + 1.0;
    color *= scanline;

    // Vignette effect
    vec2 vignetteUV = gl_FragCoord.xy / resolution.xy;
    float vignette = 1.0 - length(vignetteUV - 0.5) * 0.5;
    color *= vignette;

    // Add some random noise
    color += vec3(random(gl_FragCoord.xy + time) * 0.02);

    gl_FragColor = vec4(color, 1.0);
}
