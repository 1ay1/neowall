#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

// Simple pseudo-random function
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Create grid effect
    vec2 grid = uv * vec2(80.0, 50.0);
    vec2 gridCell = floor(grid);

    // Random values for each cell
    float cellRandom = random(gridCell);

    // Falling effect - use time and random offset
    float fall = fract(time * (0.5 + cellRandom * 0.5) + cellRandom);

    // Brightness based on fall position
    float brightness = 0.0;
    float cellY = fract(grid.y);

    // Trail effect
    if (cellY > fall - 0.3 && cellY < fall) {
        brightness = (cellY - (fall - 0.3)) / 0.3;
    }

    // Brightest at the head
    if (cellY > fall && cellY < fall + 0.05) {
        brightness = 1.0;
    }

    // Matrix green color
    vec3 color = vec3(0.0, brightness * 0.8, 0.0);

    // Add slight variation
    color.g += random(gridCell + vec2(time)) * 0.1 * brightness;

    gl_FragColor = vec4(color, 1.0);
}
