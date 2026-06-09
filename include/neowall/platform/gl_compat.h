#ifndef NEOWALL_PLATFORM_GL_COMPAT_H
#define NEOWALL_PLATFORM_GL_COMPAT_H

/*
 * Cross-platform OpenGL header selection.
 *
 * Linux:  <GL/gl.h> with GL_GLEXT_PROTOTYPES (set project-wide) exposes the
 *         full desktop GL 3.3 function set directly.
 * macOS:  OpenGL.framework's <OpenGL/gl3.h> declares core-profile 3.2..4.1
 *         entry points directly (no loader). Apple marked GL deprecated in
 *         10.14 but it remains fully functional; silence the noise.
 */

#ifdef __APPLE__
  #define GL_SILENCE_DEPRECATION 1
  #include <OpenGL/gl3.h>
  #include <OpenGL/gl3ext.h>
#else
  #include <GL/gl.h>
#endif

#endif /* NEOWALL_PLATFORM_GL_COMPAT_H */
