/* glrender_platform_wgl.c — Windows / WGL backend (OpenGL 3.3 Core via Win32 HWND).
 * Entire file is guarded so non-Windows builds produce an empty translation unit.
 */
#if defined(_WIN32)

#include "glrender_internal.h"

/* WGL extension entry points (the Windows SDK ships no wglext.h): typed locally. */
typedef HGLRC (WINAPI *zcsr_wgl_create_context_attribs_fn)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI *zcsr_wgl_swap_interval_fn)(int);

#define ZCSR_WGL_CONTEXT_MAJOR_VERSION_ARB   0x2091
#define ZCSR_WGL_CONTEXT_MINOR_VERSION_ARB   0x2092
#define ZCSR_WGL_CONTEXT_PROFILE_MASK_ARB    0x9126
#define ZCSR_WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

static GLADgenericproc zcsr_wgl_load(const char* name) {
    return (GLADgenericproc)wglGetProcAddress(name);
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    return r && r->hdc && r->ctx && wglMakeCurrent(r->hdc, r->ctx) == TRUE;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    RECT rect;
    if (!r || !r->hwnd) return;
    if (GetClientRect(r->hwnd, &rect)) {
        r->width = (rect.right - rect.left) > 0 ? (rect.right - rect.left) : 1;
        r->height = (rect.bottom - rect.top) > 0 ? (rect.bottom - rect.top) : 1;
    }
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    HWND hwnd = (HWND)zcsr_surface_native_handle(surface);
    HDC hdc;
    PIXELFORMATDESCRIPTOR pfd;
    int pf;
    HGLRC tmp;
    HGLRC ctx;
    zcsr_wgl_create_context_attribs_fn create33;
    int attribs[] = {
        ZCSR_WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        ZCSR_WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        ZCSR_WGL_CONTEXT_PROFILE_MASK_ARB, ZCSR_WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    if (!hwnd) return false;
    hdc = GetDC(hwnd);
    if (!hdc) return false;

    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        ReleaseDC(hwnd, hdc);
        return false;
    }

    /* A legacy GL context is required before wglCreateContextAttribsARB can be queried; it is
     * used only to fetch the extension, then discarded in favour of the 3.3 Core context. */
    tmp = wglCreateContext(hdc);
    if (!tmp) {
        ReleaseDC(hwnd, hdc);
        return false;
    }
    if (!wglMakeCurrent(hdc, tmp)) {
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, hdc);
        return false;
    }
    create33 = (zcsr_wgl_create_context_attribs_fn)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!create33) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, hdc);
        return false;
    }
    ctx = create33(hdc, NULL, attribs);
    if (!ctx) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, hdc);
        return false;
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(tmp);
    if (!wglMakeCurrent(hdc, ctx)) {
        wglDeleteContext(ctx);
        ReleaseDC(hwnd, hdc);
        return false;
    }

    if (!gladLoadGLLoader(zcsr_wgl_load)) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(ctx);
        ReleaseDC(hwnd, hdc);
        return false;
    }

    r->hwnd = hwnd;
    r->hdc = hdc;
    r->ctx = ctx;
    zcsr_gl_update_size(r);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (r->ctx) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(r->ctx);
        r->ctx = NULL;
    }
    if (r->hdc && r->hwnd) {
        ReleaseDC(r->hwnd, r->hdc);
        r->hdc = NULL;
    }
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    if (r && r->hdc) SwapBuffers(r->hdc);
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    zcsr_wgl_swap_interval_fn set_interval;
    if (!r) return;
    set_interval = (zcsr_wgl_swap_interval_fn)wglGetProcAddress("wglSwapIntervalEXT");
    if (set_interval) set_interval(on ? 1 : 0);
}

#else
/* Non-Windows builds: keep the translation unit non-empty (avoids MSVC C4206). */
typedef int zcsr_gl_platform_wgl_pad;
#endif /* defined(_WIN32) */
