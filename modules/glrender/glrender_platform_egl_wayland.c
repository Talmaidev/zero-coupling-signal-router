/* glrender_platform_egl_wayland.c — Linux / EGL on Wayland backend (OpenGL 3.3 Core).
 *
 * Reuses the Android EGL backend pattern but creates the EGL surface from a
 * wl_egl_window (wl_egl_window_create) instead of eglCreateWindowSurface(ANativeWindow).
 *
 * Entire file is gated: only compiles on Linux with ZCSR_LINUX_BACKEND_WAYLAND.
 * GLX backend (glrender_platform_glx.c) is excluded by the same gate.
 *
 * Lifecycle:
 *   platform_init  — eglGetDisplay(wl_display) → eglInitialize → eglBindAPI(GL)
 *                    → eglChooseConfig → wl_egl_window_create → eglCreateWindowSurface
 *                    → eglCreateContext(3.3 core) → eglMakeCurrent → gladLoadGLLoader
 *   platform_destroy — eglMakeCurrent(NULL) → destroy context/surface → wl_egl_window_destroy
 *                    → eglTerminate (reverse order)
 */
#if defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)

#include "glrender_internal.h"

#include <wayland-client.h>
#include <wayland-egl.h>
#include <string.h>

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    EGLint major = 0, minor = 0;
    EGLConfig config = NULL;
    EGLint n = 0;
    struct wl_display* wl_dpy;
    struct wl_surface* wl_surf;
    int w, h;

    if (!r || !surface) return false;

    wl_dpy = (struct wl_display*)zcsr_surface_native_display(surface);
    wl_surf = (struct wl_surface*)zcsr_surface_native_handle(surface);
    if (!wl_dpy || !wl_surf) return false;

    r->egl_display = EGL_NO_DISPLAY;
    r->egl_surface = EGL_NO_SURFACE;
    r->egl_context = EGL_NO_CONTEXT;
    r->egl_window = NULL;

    r->egl_display = eglGetDisplay((EGLNativeDisplayType)wl_dpy);
    if (r->egl_display == EGL_NO_DISPLAY) return false;

    if (!eglInitialize(r->egl_display, &major, &minor)) goto fail_init;
    if (!eglBindAPI(EGL_OPENGL_API)) goto fail_init;

    {
        EGLint cfg_attrs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
            EGL_NONE
        };
        if (!eglChooseConfig(r->egl_display, cfg_attrs, &config, 1, &n) || n < 1)
            goto fail_init;
    }

    w = zcsr_surface_width(surface);
    h = zcsr_surface_height(surface);
    r->egl_window = wl_egl_window_create(wl_surf, w > 0 ? w : 800, h > 0 ? h : 600);
    if (!r->egl_window) goto fail_init;

    r->egl_surface = eglCreateWindowSurface(r->egl_display, config,
                        (EGLNativeWindowType)r->egl_window, NULL);
    if (r->egl_surface == EGL_NO_SURFACE) goto fail_window;

    {
        EGLint ctx_attrs[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 3,
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };
        r->egl_context = eglCreateContext(r->egl_display, config, EGL_NO_CONTEXT, ctx_attrs);
    }
    if (r->egl_context == EGL_NO_CONTEXT) goto fail_surface;

    if (!eglMakeCurrent(r->egl_display, r->egl_surface, r->egl_surface, r->egl_context))
        goto fail_context;

    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress)) goto fail_context;

    r->width = w > 0 ? w : 800;
    r->height = h > 0 ? h : 600;
    return true;

fail_context:
    eglDestroyContext(r->egl_display, r->egl_context);
    r->egl_context = EGL_NO_CONTEXT;
fail_surface:
    eglDestroySurface(r->egl_display, r->egl_surface);
    r->egl_surface = EGL_NO_SURFACE;
fail_window:
    wl_egl_window_destroy(r->egl_window);
    r->egl_window = NULL;
fail_init:
    eglTerminate(r->egl_display);
    r->egl_display = EGL_NO_DISPLAY;
    return false;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    if (r->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(r->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (r->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(r->egl_display, r->egl_context);
        if (r->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(r->egl_display, r->egl_surface);
        eglTerminate(r->egl_display);
    }
    if (r->egl_window) wl_egl_window_destroy(r->egl_window);
    r->egl_display = EGL_NO_DISPLAY;
    r->egl_context = EGL_NO_CONTEXT;
    r->egl_surface = EGL_NO_SURFACE;
    r->egl_window = NULL;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->egl_display != EGL_NO_DISPLAY
        && eglMakeCurrent(r->egl_display, r->egl_surface, r->egl_surface, r->egl_context) == EGL_TRUE;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    int w, h;
    if (!r || !r->surface) return;
    w = zcsr_surface_width(r->surface);
    h = zcsr_surface_height(r->surface);
    if ((w != r->width || h != r->height) && r->egl_window && w > 0 && h > 0) {
        wl_egl_window_resize(r->egl_window, w, h, 0, 0);
        r->width = w;
        r->height = h;
    }
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (r && r->egl_display != EGL_NO_DISPLAY)
        eglSwapBuffers(r->egl_display, r->egl_surface);
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    if (r && r->egl_display != EGL_NO_DISPLAY)
        eglSwapInterval(r->egl_display, on ? 1 : 0);
}

#endif /* defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_WAYLAND) */
