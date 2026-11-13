/* glrender_platform_glx.c — Linux / X11 的 OpenGL 3.3 Core 后端（GLX）。
 *
 * 与 glrender_platform_egl_wayland.c 一一对应，只是把 "EGL 表面 + wl_egl_window"
 * 换成 "GLX 上下文 + X11 Window"：
 *   platform_init    — glXChooseFBConfig → glXCreateContextAttribsARB(3.3 core)
 *                      → glXMakeCurrent → gladLoadGLLoader(glXGetProcAddressARB)
 *   platform_destroy — glXMakeCurrent(None) → glXDestroyContext
 *   update_size      — 从 platform 取客户区尺寸；GLX 由 X server 隐式处理重绘
 *   swap             — glXSwapBuffers
 *   vsync            — glXSwapIntervalEXT（缺扩展时退回 glXSwapIntervalMESA）
 *
 * 只在 CMake 选中 X11 后端时编译（ZCSR_LINUX_BACKEND_X11）。
 */
#if defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11)

#include "glrender_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

typedef GLXContext (*PFN_glXCreateContextAttribsARB)(Display*, GLXFBConfig, GLXContext,
                                                     Bool, const int*);
typedef void (*PFN_glXSwapIntervalEXT)(Display*, GLXDrawable, int);
typedef int  (*PFN_glXSwapIntervalMESA)(unsigned);

static void* glx_proc(const char* name) {
    /* glXGetProcAddressARB 返回函数指针；用 union 避免函数指针 ↔ void* 的告警。 */
    union { void* p; void (*f)(void); } u;
    u.f = (void (*)(void))glXGetProcAddressARB((const GLubyte*)name);
    return u.p;
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface);
bool zcsr_gl_make_current(zcsr_gl_renderer* r);

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    Display* dpy;
    Window win;
    GLXFBConfig* configs = NULL;
    int n_configs = 0;
    int major = 0, minor = 0;
    PFN_glXCreateContextAttribsARB create_attribs;
    int ctx_attrs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    static const int fb_attrs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 0, GLX_STENCIL_SIZE, 0,
        GLX_DOUBLEBUFFER, True,
        None
    };

    if (!r || !surface) return false;

    dpy = (Display*)zcsr_surface_native_display(surface);
    win = (Window)(uintptr_t)zcsr_surface_native_handle(surface);
    if (!dpy || !win) return false;

    r->glx_display = dpy;
    r->glx_window = (unsigned long)win;
    r->glx_context = NULL;

    if (!glXQueryVersion(dpy, &major, &minor)) {
        fprintf(stderr, "[glx] server has no GLX extension\n");
        return false;
    }

    configs = glXChooseFBConfig(dpy, DefaultScreen(dpy), fb_attrs, &n_configs);
    if (!configs || n_configs < 1) {
        fprintf(stderr, "[glx] glXChooseFBConfig found no RGBA/double-buffered config\n");
        if (configs) XFree(configs);
        return false;
    }
    r->glx_fbconfig = configs[0];
    XFree(configs);

    create_attribs = (PFN_glXCreateContextAttribsARB)glx_proc("glXCreateContextAttribsARB");
    if (create_attribs) {
        r->glx_context = create_attribs(dpy, r->glx_fbconfig, NULL, True, ctx_attrs);
    }
    if (!r->glx_context) {
        /* 没有 ARB_create_context 就没有 3.3 core；引擎只支持 core profile，
         * 不做静默降级（否则 VAO 路径会在运行时崩）。 */
        fprintf(stderr, "[glx] GLX_ARB_create_context unavailable / 3.3 core rejected\n");
        return false;
    }

    if (!glXMakeCurrent(dpy, win, r->glx_context)) {
        fprintf(stderr, "[glx] glXMakeCurrent failed\n");
        glXDestroyContext(dpy, r->glx_context);
        r->glx_context = NULL;
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)glx_proc)) {
        fprintf(stderr, "[glx] gladLoadGLLoader failed\n");
        glXMakeCurrent(dpy, None, NULL);
        glXDestroyContext(dpy, r->glx_context);
        r->glx_context = NULL;
        return false;
    }

    zcsr_surface_get_client_size(surface, &r->width, &r->height);
    if (r->width <= 0)  r->width = 800;
    if (r->height <= 0) r->height = 600;
    r->glx_swap_interval_set = -1;
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (!r) return;
    if (r->glx_display) {
        Display* dpy = (Display*)r->glx_display;
        glXMakeCurrent(dpy, None, NULL);
        if (r->glx_context) glXDestroyContext(dpy, r->glx_context);
    }
    r->glx_context = NULL;
    r->glx_fbconfig = NULL;
    r->glx_display = NULL;
    r->glx_window = 0;
    r->width = 0;
    r->height = 0;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    Display* dpy;
    if (!r || !r->glx_display || !r->glx_context) return false;
    dpy = (Display*)r->glx_display;
    return glXMakeCurrent(dpy, (Window)(uintptr_t)r->glx_window, r->glx_context) ? true : false;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    int w = 0, h = 0;
    if (!r || !r->surface) return;
    zcsr_surface_get_client_size(r->surface, &w, &h);
    if (w > 0 && h > 0) {
        r->width = w;
        r->height = h;
    }
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    Display* dpy;
    if (!r || !r->glx_display) return;
    dpy = (Display*)r->glx_display;
    glXSwapBuffers(dpy, (Window)(uintptr_t)r->glx_window);
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    Display* dpy;
    const char* exts;
    if (!r || !r->glx_display) return;
    dpy = (Display*)r->glx_display;
    exts = glXQueryExtensionsString(dpy, DefaultScreen(dpy));
    if (!exts) return;

    if (strstr(exts, "GLX_EXT_swap_control")) {
        PFN_glXSwapIntervalEXT set_ext =
            (PFN_glXSwapIntervalEXT)glx_proc("glXSwapIntervalEXT");
        if (set_ext) {
            set_ext(dpy, (GLXDrawable)(uintptr_t)r->glx_window, on ? 1 : 0);
            r->glx_swap_interval_set = on ? 1 : 0;
            return;
        }
    }
    if (strstr(exts, "GLX_MESA_swap_control")) {
        PFN_glXSwapIntervalMESA set_mesa =
            (PFN_glXSwapIntervalMESA)glx_proc("glXSwapIntervalMESA");
        if (set_mesa) {
            set_mesa(on ? 1u : 0u);
            r->glx_swap_interval_set = on ? 1 : 0;
        }
    }
}

#endif /* defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11) */

#if !(defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11))
typedef int zcsr_gl_platform_glx_unused_tu_;
#endif
