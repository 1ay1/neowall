// The author declares the manifest uniform themselves (as the docs suggest).
// Must compile (it used to fail: "declaration conflicts with previous one").
uniform float uLevel;
/* uniform float uLevel;  ← commented-out copies don't count */
void mainImage(out vec4 c, in vec2 f) { c = vec4(vec3(uLevel), 1.0); }
