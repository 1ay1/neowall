#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Animated gradient colors
    vec3 color1 = vec3(
        sin(time * 0.3) * 0.5 + 0.5,
        cos(time * 0.2) * 0.5 + 0.5,
        sin(time * 0.4) * 0.5 + 0.5
    );

    vec3 color2 = vec3(
        cos(time * 0.25) * 0.5 + 0.5,
        sin(time * 0.35) * 0.5 + 0.5,
        cos(time * 0.3) * 0.5 + 0.5
    );

    // Create diagonal gradient that shifts over time
    float gradientAngle = time * 0.1;
    float gradient = uv.x * cos(gradientAngle) + uv.y * sin(gradientAngle);

    // Mix colors
    vec3 color = mix(color1, color2, gradient);

    gl_FragColor = vec4(color, 1.0);
}
