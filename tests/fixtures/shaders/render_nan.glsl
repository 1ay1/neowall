// Deliberately produces NaN in a band of Buffer A (a real bug pattern: a
// width term that goes negative, exp() of a huge positive, times step()==0).
// `neowall render --check` must find it in Buffer A and exit 2.
// Buffer A
void mainImage(out vec4 c, in vec2 f) {
    vec2 q = f / iResolution.y - vec2(0.5, 0.6);
    float cone = step(q.y, -0.02) * exp(-abs(q.x) / (0.004 + (-q.y - 0.02) * 0.25));
    c = vec4(vec3(0.1) + cone * 0.1, 1.0);
}
// Image
void mainImage(out vec4 c, in vec2 f) {
    c = vec4(texture(iChannel0, f / iResolution.xy).rgb, 1.0);
}
