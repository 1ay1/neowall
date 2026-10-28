void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    float t = iTime;
    t += 0.1;  // This is OK - local variable
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = 0.5 + 0.5 * cos(t + uv.xyx + vec3(0, 2, 4));
    fragColor = vec4(col, 1.0);
}
