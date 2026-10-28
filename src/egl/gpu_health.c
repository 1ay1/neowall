#include "neowall/egl/gpu_health.h"

#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdlib.h>
#include <string.h>

#include "neowall/neowall.h"

/* Not in every header set; values are fixed by the specs. */
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_GUILTY_CONTEXT_RESET
#define GL_GUILTY_CONTEXT_RESET   0x8253
#define GL_INNOCENT_CONTEXT_RESET 0x8254
#define GL_UNKNOWN_CONTEXT_RESET  0x8255
#endif
#ifndef EGL_CONTEXT_OPENGL_ROBUST_ACCESS
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS 0x31B2
#endif
#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY 0x31BD
#endif
#ifndef EGL_LOSE_CONTEXT_ON_RESET
#define EGL_LOSE_CONTEXT_ON_RESET 0x31BF
#endif

typedef GLenum (*reset_status_fn)(void);
typedef void (*debug_cb_fn)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void *);
typedef void (*debug_cb_set_fn)(debug_cb_fn, const void *);

static reset_status_fn g_reset_status;
static bool g_lost;

static bool has_ext(const char *list, const char *name) {
    if (!list) return false;
    size_t n = strlen(name);
    for (const char *p = list; (p = strstr(p, name)) != NULL; p += n) {
        bool start = p == list || p[-1] == ' ';
        bool end = p[n] == ' ' || p[n] == '\0';
        if (start && end) return true;
    }
    return false;
}

bool gpu_health_context_attribs(EGLDisplay dpy, EGLint *a, int cap) {
    int i = 0;
    if (cap < 11) return false;
    a[i++] = EGL_CONTEXT_MAJOR_VERSION; a[i++] = 3;
    a[i++] = EGL_CONTEXT_MINOR_VERSION; a[i++] = 3;
    a[i++] = EGL_CONTEXT_OPENGL_PROFILE_MASK; a[i++] = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;

    /* EGL 1.5 core and EGL_KHR_create_context both spell robustness the same. */
    EGLint major = 0, minor = 0;
    const char *ver = eglQueryString(dpy, EGL_VERSION);
    if (ver) {
        major = atoi(ver);
        const char *dot = strchr(ver, '.');
        if (dot) minor = atoi(dot + 1);
    }
    bool robust = (major > 1 || (major == 1 && minor >= 5)) ||
                  has_ext(eglQueryString(dpy, EGL_EXTENSIONS), "EGL_KHR_create_context");
    if (robust) {
        a[i++] = EGL_CONTEXT_OPENGL_ROBUST_ACCESS; a[i++] = EGL_TRUE;
        a[i++] = EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY; a[i++] = EGL_LOSE_CONTEXT_ON_RESET;
    }
    a[i] = EGL_NONE;
    return robust;
}

static void debug_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                          GLsizei len, const GLchar *msg, const void *user) {
    (void)source; (void)id; (void)len; (void)user;
    /* 0x9146 HIGH, 0x9147 MEDIUM, 0x9148 LOW, 0x826B NOTIFICATION */
    if (severity == 0x826B) return;
    if (type == 0x824C /* ERROR */ || severity == 0x9146) {
        log_error("GL: %s", msg);
    } else {
        log_warn("GL: %s", msg);
    }
}

void gpu_health_init(bool robust, bool verbose) {
    g_lost = false;
    g_reset_status = NULL;
    union { void (*fn)(void); reset_status_fn rs; debug_cb_set_fn dbg; } u;

    if (robust) {
        u.fn = eglGetProcAddress("glGetGraphicsResetStatus");
        if (!u.fn) u.fn = eglGetProcAddress("glGetGraphicsResetStatusARB");
        if (!u.fn) u.fn = eglGetProcAddress("glGetGraphicsResetStatusKHR");
        g_reset_status = u.rs;
    }
    log_info("GPU reset detection: %s", g_reset_status ? "on" : "unavailable");

    const char *env = getenv("NEOWALL_GL_DEBUG");
    if (verbose || (env && env[0] == '1')) {
        u.fn = eglGetProcAddress("glDebugMessageCallback");
        if (u.fn) {
            glEnable(0x92E0 /* GL_DEBUG_OUTPUT */);
            u.dbg(debug_message, NULL);
            log_info("GL debug output on");
        }
    }
}

bool gpu_health_status_is_lost(unsigned status) {
    return status == GL_GUILTY_CONTEXT_RESET || status == GL_INNOCENT_CONTEXT_RESET ||
           status == GL_UNKNOWN_CONTEXT_RESET;
}

bool gpu_health_lost(void) {
    if (g_lost) return true;
    if (!g_reset_status) return false;
    GLenum s = g_reset_status();
    if (!gpu_health_status_is_lost(s)) return false;
    g_lost = true;
    log_error("GPU reset detected (%s); the GL context is gone",
              s == GL_GUILTY_CONTEXT_RESET ? "caused by neowall" :
              s == GL_INNOCENT_CONTEXT_RESET ? "caused by another program" : "cause unknown");
    return true;
}

bool gpu_health_swap_result(uint32_t *streak, bool ok) {
    if (!streak) return false;
    if (ok) {
        *streak = 0;
        return false;
    }
    if (*streak < UINT32_MAX) (*streak)++;
    return *streak == NW_GPU_SWAP_FAIL_LIMIT;
}
