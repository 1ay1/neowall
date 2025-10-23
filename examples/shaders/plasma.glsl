#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Create plasma effect using sine waves
    float plasma = 0.0;

    // Multiple sine waves with different frequencies
    plasma += sin(uv.x * 10.0 + time * 3.0);
    plasma += sin(uv.y * 10.0 + time * 3.0);
    plasma += sin((uv.x + uv.y) * 10.0 + time * 3.0);
    plasma += sin(length(uv - 0.5) * 20.0 + time * 3.0);

    // Normalize
    plasma *= 0.25;

    // Create color based on plasma value
    vec3 color;
    color.r = sin(plasma * 3.14159 + time * 2.0) * 0.5 + 0.5;
    color.g = sin(plasma * 3.14159 + time * 1.5 + 2.0) * 0.5 + 0.5;
    color.b = sin(plasma * 3.14159 + time * 2.5 + 4.0) * 0.5 + 0.5;

    gl_FragColor = vec4(color, 1.0);
}
