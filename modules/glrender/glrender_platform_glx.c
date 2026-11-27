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
 * 回落：父窗口视觉无匹配 FBConfig 时，在其上自建子窗口作为 GL drawable，事件/输入仍归父窗口。
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
    /* glXGetProcAddressARB returns a function pointer; use union to cross the
     * function-pointer/object-pointer boundary without a cast. */
    union { void* p; void (*f)(void); } u;
    u.f = (void (*)(void))glXGetProcAddressARB((const GLubyte*)name);
    return u.p;
}

/* C does not allow casting void* to a function pointer. Copy the address into
 * the correctly typed slot; glXGetProcAddressARB and void* have the same
 * representation on the POSIX/X11 targets this backend supports. */
static void glx_proc_copy(const char* name, void* out, size_t out_size) {
    void* raw = glx_proc(name);
    memset(out, 0, out_size);
    memcpy(out, &raw, out_size < sizeof(raw) ? out_size : sizeof(raw));
}

/* Fallback child drawable. The engine creates a single GLX renderer per process;
 * this state lives in this translation unit so the renderer struct in the
 * internal header does not grow an X11-only field. When 0, the GLX drawable is
 * the SDL-provided parent window; otherwise it is the child window created on
 * the fallback FBConfig's visual. Events/input still belong to the parent. */
static Window s_glx_child_window = 0;

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface);
bool zcsr_gl_make_current(zcsr_gl_renderer* r);

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    Display* dpy;
    Window win;
    GLXFBConfig* configs = NULL;
    GLXFBConfig chosen = NULL;
    int n_configs = 0;
    int i;
    VisualID window_visual = 0;
    XWindowAttributes window_attrs;
    int major = 0, minor = 0;
    bool visual_matched = false;
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

    /* The X11 window was created with CopyFromParent visual. On hybrid laptops
     * the first FBConfig returned by glXChooseFBConfig is not necessarily the
     * one matching that visual; using it can make glXMakeCurrent fail or draw
     * to a different visual. Select the config whose GLX_VISUAL_ID matches the
     * actual window, which is the same path for Intel iris and AMD radeonsi. */
    if (XGetWindowAttributes(dpy, win, &window_attrs) && window_attrs.visual)
        window_visual = window_attrs.visual->visualid;

    configs = glXChooseFBConfig(dpy, DefaultScreen(dpy), fb_attrs, &n_configs);
    if (!configs || n_configs < 1) {
        fprintf(stderr, "[glx] glXChooseFBConfig found no RGBA/double-buffered config\n");
        if (configs) XFree(configs);
        return false;
    }
    for (i = 0; i < n_configs; ++i) {
        XVisualInfo* vi;
        int double_buffer = 0;
        int caveat = 0;
        glXGetFBConfigAttrib(dpy, configs[i], GLX_DOUBLEBUFFER, &double_buffer);
        glXGetFBConfigAttrib(dpy, configs[i], GLX_CONFIG_CAVEAT, &caveat);
        if (!double_buffer || caveat == GLX_SLOW_CONFIG) continue;
        vi = glXGetVisualFromFBConfig(dpy, configs[i]);
        if (vi) {
            bool match = (window_visual == 0 || vi->visualid == window_visual);
            XFree(vi);
            if (match) { chosen = configs[i]; visual_matched = true; break; }
        }
    }
    if (!chosen) {
        /* No FBConfig shares the parent window's visual (hybrid PRIME offload:
         * the NVIDIA FBConfig visuals do not match the Mesa/modesetting root
         * visual). Do not fatal: pick the first non-slow double-buffered
         * RGBA8 config; the GLX drawable will be a child window on that
         * config's visual instead of the parent SDL window. */
        for (i = 0; i < n_configs; ++i) {
            int double_buffer = 0, caveat = 0;
            glXGetFBConfigAttrib(dpy, configs[i], GLX_DOUBLEBUFFER, &double_buffer);
            glXGetFBConfigAttrib(dpy, configs[i], GLX_CONFIG_CAVEAT, &caveat);
            if (double_buffer && caveat != GLX_SLOW_CONFIG) { chosen = configs[i]; break; }
        }
    }
    XFree(configs);
    if (!chosen) {
        fprintf(stderr, "[glx] no FBConfig matches window visual 0x%lx (Intel/AMD Mesa visual mismatch)\n",
                (unsigned long)window_visual);
        return false;
    }
    r->glx_fbconfig = chosen;

    create_attribs = NULL;
    glx_proc_copy("glXCreateContextAttribsARB", &create_attribs, sizeof(create_attribs));
    if (create_attribs) {
        r->glx_context = create_attribs(dpy, r->glx_fbconfig, NULL, True, ctx_attrs);
    }
    if (!r->glx_context) {
        /* 没有 ARB_create_context 就没有 3.3 core；引擎只支持 core profile，
         * 不做静默降级（否则 VAO 路径会在运行时崩）。 */
        fprintf(stderr, "[glx] GLX_ARB_create_context unavailable / 3.3 core rejected\n");
        return false;
    }

    /* Visual-mismatch fallback: the parent SDL window keeps its own visual and
     * owns events/input; GL draws into a child window created on the chosen
     * FBConfig's visual. On the matched path the drawable stays the parent. */
    if (window_visual != 0 && !visual_matched) {
        XVisualInfo* fvi = glXGetVisualFromFBConfig(dpy, chosen);
        Colormap child_cmap;
        XSetWindowAttributes cwa;
        Window child;
        int cw = 0, ch = 0;
        if (!fvi) {
            fprintf(stderr, "[glx] fallback: no visual for chosen FBConfig\n");
            glXDestroyContext(dpy, r->glx_context);
            r->glx_context = NULL;
            return false;
        }
        zcsr_surface_get_client_size(surface, &cw, &ch);
        if (cw <= 0) cw = 800;
        if (ch <= 0) ch = 600;
        child_cmap = XCreateColormap(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                                    fvi->visual, AllocNone);
        cwa.colormap = child_cmap;
        cwa.event_mask = ExposureMask;
        child = XCreateWindow(dpy, win, 0, 0, cw, ch, 0, fvi->depth,
                              InputOutput, fvi->visual, CWColormap | CWEventMask, &cwa);
        /* The server keeps a colormap reference while the window references it;
         * the local handle can be released right after window creation. */
        XFreeColormap(dpy, child_cmap);
        XFree(fvi);
        XMapWindow(dpy, child);
        s_glx_child_window = child;
        r->glx_window = (unsigned long)child;
    } else {
        s_glx_child_window = 0;
    }

    if (!glXMakeCurrent(dpy, (Window)(uintptr_t)r->glx_window, r->glx_context)) {
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
        if (s_glx_child_window) {
            XDestroyWindow(dpy, s_glx_child_window);
            s_glx_child_window = 0;
        }
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
    /* glXMakeCurrent is a server round-trip; skip it when this context/drawable
     * is already current. Per-draw-batch make-current dominated submission. */
    if (glXGetCurrentContext() == r->glx_context &&
        glXGetCurrentDrawable() == (GLXDrawable)(uintptr_t)r->glx_window) {
        return true;
    }
    return glXMakeCurrent(dpy, (Window)(uintptr_t)r->glx_window, r->glx_context) ? true : false;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    int w = 0, h = 0;
    if (!r || !r->surface) return;
    zcsr_surface_get_client_size(r->surface, &w, &h);
    if (w > 0 && h > 0) {
        r->width = w;
        r->height = h;
        if (s_glx_child_window) {
            XMoveResizeWindow((Display*)r->glx_display, s_glx_child_window, 0, 0, w, h);
        }
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
        PFN_glXSwapIntervalEXT set_ext = NULL;
        glx_proc_copy("glXSwapIntervalEXT", &set_ext, sizeof(set_ext));
        if (set_ext) {
            set_ext(dpy, (GLXDrawable)(uintptr_t)r->glx_window, on ? 1 : 0);
            r->glx_swap_interval_set = on ? 1 : 0;
            return;
        }
    }
    if (strstr(exts, "GLX_MESA_swap_control")) {
        PFN_glXSwapIntervalMESA set_mesa = NULL;
        glx_proc_copy("glXSwapIntervalMESA", &set_mesa, sizeof(set_mesa));
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
