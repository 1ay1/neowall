#version 100
precision mediump float;

uniform float time;
uniform vec2 resolution;

void main() {
    vec2 uv = gl_FragCoord.xy / resolution.xy;

    // Center coordinates
    vec2 center = uv - 0.5;

    // Create waves based on distance from center
    float dist = length(center);
    float wave = sin(dist * 20.0 - time * 3.0) * 0.5 + 0.5;

    // Add color gradient based on position and time
    vec3 color1 = vec3(0.2, 0.4, 0.8); // Blue
    vec3 color2 = vec3(0.8, 0.3, 0.5); // Pink
    vec3 color3 = vec3(0.3, 0.8, 0.6); // Teal

    // Mix colors based on wave and angle
    float angle = atan(center.y, center.x);
    vec3 color = mix(color1, color2, wave);
    color = mix(color, color3, sin(angle * 3.0 + time) * 0.5 + 0.5);

    // Add brightness variation
    color *= wave * 0.5 + 0.5;

    gl_FragColor = vec4(color, 1.0);
}
