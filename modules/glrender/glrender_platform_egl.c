/* glrender_platform_egl.c — Android / EGL backend (OpenGL ES 3.0 via ANativeWindow).
 * Entire file is guarded so non-Android builds produce an empty translation unit.
 */
#if defined(__ANDROID__)

#include "glrender_internal.h"

#include <string.h>

static bool zcsr_gl_egl_fail(zcsr_gl_renderer* r, const char* stage) {
    EGLint error = eglGetError();
    __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
        "%s failed: EGL error=0x%x", stage, (unsigned int)error);
    zcsr_gl_platform_destroy(r);
    return false;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->egl_initialized
        && r->display != EGL_NO_DISPLAY
        && r->egl_surface != EGL_NO_SURFACE
        && r->ctx != EGL_NO_CONTEXT
        && eglMakeCurrent(r->display, r->egl_surface, r->egl_surface, r->ctx) == EGL_TRUE;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    EGLint width = 0, height = 0;
    if (!r || !r->egl_initialized || r->egl_surface == EGL_NO_SURFACE) return;
    if (eglQuerySurface(r->display, r->egl_surface, EGL_WIDTH, &width) == EGL_TRUE
        && eglQuerySurface(r->display, r->egl_surface, EGL_HEIGHT, &height) == EGL_TRUE) {
        r->width = width > 0 ? (int)width : 1;
        r->height = height > 0 ? (int)height : 1;
    }
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    ANativeWindow* window;
    EGLConfig config;
    EGLint major = 0, minor = 0, count = 0, visual_id = 0;
    const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE
    };
    const EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    const GLubyte* version;
    if (!r) return false;
    r->display = EGL_NO_DISPLAY;
    r->egl_surface = EGL_NO_SURFACE;
    r->ctx = EGL_NO_CONTEXT;
    r->egl_initialized = false;
    if (!surface) return false;
    window = (ANativeWindow*)zcsr_surface_native_handle(surface);
    if (!window) return false;
    r->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (r->display == EGL_NO_DISPLAY) return zcsr_gl_egl_fail(r, "eglGetDisplay");
    if (eglInitialize(r->display, &major, &minor) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglInitialize");
    r->egl_initialized = true;
    if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglBindAPI");
    if (eglChooseConfig(r->display, config_attrs, &config, 1, &count) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglChooseConfig");
    if (count < 1) {
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "No EGLConfig supports a GLES 3 window surface");
        zcsr_gl_platform_destroy(r);
        return false;
    }
    if (eglGetConfigAttrib(r->display, config, EGL_NATIVE_VISUAL_ID, &visual_id) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglGetConfigAttrib");
    if (ANativeWindow_setBuffersGeometry(window, 0, 0, visual_id) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "ANativeWindow_setBuffersGeometry failed");
        zcsr_gl_platform_destroy(r);
        return false;
    }
    r->egl_surface = eglCreateWindowSurface(r->display, config,
        (EGLNativeWindowType)window, NULL);
    if (r->egl_surface == EGL_NO_SURFACE)
        return zcsr_gl_egl_fail(r, "eglCreateWindowSurface");
    r->ctx = eglCreateContext(r->display, config, EGL_NO_CONTEXT, context_attrs);
    if (r->ctx == EGL_NO_CONTEXT)
        return zcsr_gl_egl_fail(r, "eglCreateContext");
    if (eglMakeCurrent(r->display, r->egl_surface, r->egl_surface, r->ctx) != EGL_TRUE)
        return zcsr_gl_egl_fail(r, "eglMakeCurrent");
    version = glGetString(GL_VERSION);
    if (!version || strncmp((const char*)version, "OpenGL ES 3.", 12) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "Expected GLES 3.x, actual version: %s",
            version ? (const char*)version : "(null)");
        zcsr_gl_platform_destroy(r);
        return false;
    }
    zcsr_gl_update_size(r);
    __android_log_print(ANDROID_LOG_INFO, "zcsr-gl",
        "EGL %d.%d; GLES=%s; size=%dx%d",
        major, minor, (const char*)version, r->width, r->height);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    if (r->display != EGL_NO_DISPLAY && r->egl_initialized) {
        (void)eglMakeCurrent(r->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (r->egl_surface != EGL_NO_SURFACE) (void)eglDestroySurface(r->display, r->egl_surface);
        if (r->ctx != EGL_NO_CONTEXT) (void)eglDestroyContext(r->display, r->ctx);
        (void)eglTerminate(r->display);
    }
    r->display = EGL_NO_DISPLAY;
    r->egl_surface = EGL_NO_SURFACE;
    r->ctx = EGL_NO_CONTEXT;
    r->egl_initialized = false;
    r->width = 0;
    r->height = 0;
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (!r || !r->egl_initialized || r->egl_surface == EGL_NO_SURFACE) return;
    if (eglSwapBuffers(r->display, r->egl_surface) != EGL_TRUE) {
        EGLint error = eglGetError();
        __android_log_print(ANDROID_LOG_ERROR, "zcsr-gl",
            "eglSwapBuffers failed: 0x%x", (unsigned int)error);
    }
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    if (!r || !r->egl_initialized) return;
    (void)eglSwapInterval(r->display, on ? 1 : 0);
}

#else
/* Non-Android builds: keep the translation unit non-empty (avoids MSVC C4206). */
typedef int zcsr_gl_platform_egl_pad;
#endif /* defined(__ANDROID__) */
