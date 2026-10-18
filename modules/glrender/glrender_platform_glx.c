/* glrender_platform_glx.c — Linux / GLX backend (OpenGL 3.3 Core via X11).
 * Entire file is guarded so non-Linux builds produce an empty translation unit.
 */
#if defined(__linux__) && !defined(__ANDROID__)

#include "glrender_internal.h"

#include <GL/glxext.h> /* GLX_CONTEXT_*_ARB + 3.3 core profile attributes */
#include <stdint.h>

typedef GLXContext (*zcsr_glx_create_context_attribs_fn)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
typedef void (*zcsr_glx_swap_interval_ext_fn)(Display*, GLXDrawable, int);
typedef int (*zcsr_glx_swap_interval_mesa_fn)(unsigned int);
typedef int (*zcsr_glx_swap_interval_sgi_fn)(int);

static GLADgenericproc zcsr_glx_load(const char* name) {
    return (GLADgenericproc)glXGetProcAddressARB((const GLubyte*)name);
}

static GLXFBConfig zcsr_gl_choose_config(Display* display, int screen, VisualID visual_id) {
    int attrs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 0,
        GLX_STENCIL_SIZE, 0,
        GLX_DOUBLEBUFFER, True,
        None
    };
    int count = 0;
    int i;
    GLXFBConfig result = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display, screen, attrs, &count);
    if (!configs) return 0;
    for (i = 0; i < count; ++i) {
        XVisualInfo* info = glXGetVisualFromFBConfig(display, configs[i]);
        if (info) {
            if (info->visualid == visual_id && !result) result = configs[i];
            XFree(info);
        }
    }
    XFree(configs);
    return result;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->display && r->window && r->ctx && glXMakeCurrent(r->display, r->window, r->ctx);
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    XWindowAttributes a;
    if (!r || !r->display || !r->window) return;
    if (XGetWindowAttributes(r->display, r->window, &a)) {
        r->width = a.width > 0 ? a.width : 1;
        r->height = a.height > 0 ? a.height : 1;
    }
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    Display* display = (Display*)zcsr_surface_native_display(surface);
    Window window = (Window)(uintptr_t)zcsr_surface_native_handle(surface);
    XWindowAttributes attrs;
    GLXFBConfig config;
    zcsr_glx_create_context_attribs_fn create_context;
    int context_attrs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
    int screen;
    if (!display || !window || !XGetWindowAttributes(display, window, &attrs)) return false;
    screen = DefaultScreen(display);
    config = zcsr_gl_choose_config(display, screen, XVisualIDFromVisual(attrs.visual));
    if (!config) return false;
    create_context = (zcsr_glx_create_context_attribs_fn)
        glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
    if (!create_context) return false;

    r->display = display;
    r->window = window;
    r->ctx = create_context(display, config, 0, True, context_attrs);
    if (!r->ctx) return false;
    if (!zcsr_gl_make_current(r)) {
        zcsr_gl_platform_destroy(r);
        return false;
    }
    if (!gladLoadGLLoader(zcsr_glx_load)) {
        zcsr_gl_platform_destroy(r);
        return false;
    }
    zcsr_gl_update_size(r);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (r->display && r->ctx) {
        glXMakeCurrent(r->display, None, 0);
        glXDestroyContext(r->display, r->ctx);
    }
    r->ctx = 0;
    r->display = 0;
    r->window = 0;
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (r && r->display && r->window) glXSwapBuffers(r->display, r->window);
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    zcsr_glx_swap_interval_ext_fn ext;
    zcsr_glx_swap_interval_mesa_fn mesa;
    zcsr_glx_swap_interval_sgi_fn sgi;
    if (!r || !zcsr_gl_make_current(r)) return;
    ext = (zcsr_glx_swap_interval_ext_fn)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
    if (ext) { ext(r->display, r->window, on ? 1 : 0); return; }
    mesa = (zcsr_glx_swap_interval_mesa_fn)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
    if (mesa) { mesa(on ? 1u : 0u); return; }
    sgi = (zcsr_glx_swap_interval_sgi_fn)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI");
    if (sgi && on) (void)sgi(1);
}

#else
/* Non-Linux builds: keep the translation unit non-empty (avoids MSVC C4206). */
typedef int zcsr_gl_platform_glx_pad;
#endif /* defined(__linux__) && !defined(__ANDROID__) */
