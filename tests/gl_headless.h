#ifndef NEOWALL_TEST_GL_HEADLESS_H
#define NEOWALL_TEST_GL_HEADLESS_H

/* Make a GL 3.3 core context current with no display. Returns NULL on
 * success, or why it couldn't (tests then skip with exit code 77). */
const char *gl_headless_init(void);
void gl_headless_fini(void);

#endif
