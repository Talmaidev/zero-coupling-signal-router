/* Agent 5 — Platform abstraction. Win32 / X11 / Cocoa, gated on ZCSR_PLATFORM (zcsr/platform.h).
 * No OpenGL/DirectX/Vulkan. */
#include "zcsr/platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#if defined(__ANDROID__)
#include <android/native_window.h>
#endif

struct zcsr_surface {
    bool in_use;
    float dpi_scale;
    zcsr_dpi_changed_fn dpi_cb;
    void* dpi_user;
    int min_w, min_h, max_w, max_h; /* 0 = no limit; client-area pixels */
    zcsr_focus_fn focus_cb;
    void* focus_user;
#if defined(__ANDROID__)
    void* native_window;
#elif defined(_WIN32)
    void* hwnd;
    zcsr_window_mode mode;
    bool  fs_valid;            /* fullscreen restore snapshot present */
    long  fs_style;
    long  fs_exstyle;
    int   fs_x, fs_y, fs_w, fs_h;
    int   fs_display;
#elif defined(__linux__)
    void* display;
    unsigned long window;
    zcsr_window_mode mode;
    bool  fs_valid;
    int   fs_x, fs_y, fs_w, fs_h;
    int   fs_display;
    unsigned long xsettings_owner;
    unsigned long xsettings_settings_atom;
    unsigned long xsettings_selection_atom;
#elif defined(__APPLE__)
    void* ns_window;
#else
    void* handle;
#endif
};

enum { ZCSR_SURFACE_POOL = 4 };
static zcsr_surface zcsr_surfaces[ZCSR_SURFACE_POOL];

static zcsr_surface* zcsr_surface_acquire(void) {
    for (size_t i = 0; i < ZCSR_SURFACE_POOL; ++i) {
        if (!zcsr_surfaces[i].in_use) {
            zcsr_surfaces[i].in_use = true;
            return &zcsr_surfaces[i];
        }
    }
    return 0;
}

static void zcsr_surface_release(zcsr_surface* s) {
    if (!s) return;
    *s = (zcsr_surface){ 0 };
    s->dpi_scale = 1.0f;
}

static void zcsr_surface_update_dpi_scale(
    zcsr_surface* s,
    float new_scale
) {
    float old_scale;
    zcsr_dpi_changed_fn callback;
    void* user;
    if (!s || !s->in_use) return;
    if (!isfinite(new_scale) || new_scale <= 0.0f) return;
    old_scale = s->dpi_scale;
    if (fabsf(new_scale - old_scale) < 0.01f) return;
    s->dpi_scale = new_scale;
    callback = s->dpi_cb;
    user = s->dpi_user;
    if (callback) {
        callback(s, new_scale, user);
    }
}

void zcsr_surface_set_dpi_callback(
    zcsr_surface* s,
    zcsr_dpi_changed_fn callback,
    void* user
) {
    if (!s || !s->in_use) return;
    s->dpi_cb = callback;
    s->dpi_user = callback ? user : NULL;
}

/* Forward declaration for platform-specific size-limit application.
 * Defined in the _WIN32 and __linux__ (non-Android) blocks below. */
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
static void zcsr_surface_apply_size_limits(zcsr_surface* s);
#endif

bool zcsr_surface_set_size_limits(
    zcsr_surface* s,
    int min_w, int min_h,
    int max_w, int max_h
) {
    if (!s || !s->in_use) return false;
    /* Android / macOS / unknown platforms do not support window size limits.
     * Note: Android defines __linux__ but is excluded explicitly. */
#if !(defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)))
    return false;
#endif
    if (min_w < 0 || min_h < 0 || max_w < 0 || max_h < 0) return false;
    if ((min_w > 0 && max_w > 0 && min_w > max_w) ||
        (min_h > 0 && max_h > 0 && min_h > max_h)) return false;
    s->min_w = min_w;
    s->min_h = min_h;
    s->max_w = max_w;
    s->max_h = max_h;
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    zcsr_surface_apply_size_limits(s);
#endif
    return true;
}

static void zcsr_surface_dispatch_focus(
    zcsr_surface* s,
    zcsr_focus_event event
) {
    zcsr_focus_fn callback;
    void* user;
    if (!s || !s->in_use) return;
    if (event != ZCSR_FOCUS_GAINED && event != ZCSR_FOCUS_LOST) return;
    callback = s->focus_cb;
    user = s->focus_user;
    if (callback) {
        callback(s, event, user);
    }
}

void zcsr_surface_set_focus_callback(
    zcsr_surface* s,
    zcsr_focus_fn callback,
    void* user
) {
    if (!s || !s->in_use) return;
    s->focus_cb = callback;
    s->focus_user = callback ? user : NULL;
}

#if defined(__ANDROID__)
zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) {
    zcsr_surface* surface;
    /* Android creates the native window; zcsr creates only a logical handle. */
    (void)title;
    (void)bounds;
    surface = zcsr_surface_acquire();
    if (!surface) return 0;
    surface->native_window = 0;
    surface->dpi_scale = zcsr_surface_get_dpi_scale(surface);
    if (!isfinite(surface->dpi_scale) || surface->dpi_scale <= 0.0f) {
        surface->dpi_scale = 1.0f;
    }
    return surface;
}
void zcsr_surface_destroy(zcsr_surface* s) {
    if (!s || !s->in_use) return;
    zcsr_surface_detach_native(s);
    zcsr_surface_release(s);
}
void* zcsr_surface_native_handle(const zcsr_surface* s) {
    return (s && s->in_use) ? s->native_window : 0;
}
void* zcsr_surface_native_display(const zcsr_surface* s) {
    (void)s;
    return 0;
}
void zcsr_surface_pump_events(zcsr_surface* s) {
    /* Android lifecycle and input events belong to native_app_glue. */
    (void)s;
}

void zcsr_platform_android_dispatch_focus(zcsr_focus_event event) {
    size_t i;
    if (event != ZCSR_FOCUS_GAINED && event != ZCSR_FOCUS_LOST) return;
    for (i = 0; i < ZCSR_SURFACE_POOL; ++i) {
        zcsr_surface* s = &zcsr_surfaces[i];
        if (s->in_use) {
            zcsr_surface_dispatch_focus(s, event);
        }
    }
}
#elif defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <limits.h>
#if defined(ZCSR_HAVE_XRANDR)
#include <X11/extensions/Xrandr.h>
#endif

/* Forward declaration: defined later in the A1 display/window Linux block. */
static bool zcsr_linux_refresh_xsettings_owner(zcsr_surface* s);

static void zcsr_set_atom(Display* display, Window window, const char* property, const char* atom_name) {
    Atom prop = XInternAtom(display, property, False);
    Atom atom = XInternAtom(display, atom_name, False);
    XChangeProperty(display, window, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*)&atom, 1);
}

zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) {
    Display* display = XOpenDisplay(0);
    int screen;
    Window root;
    XVisualInfo visual_info;
    Visual* visual;
    int depth;
    Colormap colormap;
    XSetWindowAttributes attrs;
    unsigned long mask;
    Window window;
    zcsr_surface* surface;

    if (!display) return 0;
    screen = DefaultScreen(display);
    root = RootWindow(display, screen);

    if (XMatchVisualInfo(display, screen, 32, TrueColor, &visual_info)) {
        visual = visual_info.visual;
        depth = visual_info.depth;
        colormap = XCreateColormap(display, root, visual, AllocNone);
    } else {
        visual = DefaultVisual(display, screen);
        depth = DefaultDepth(display, screen);
        colormap = DefaultColormap(display, screen);
    }

    attrs.override_redirect = True;
    attrs.colormap = colormap;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    mask = CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel;

    if (bounds.w <= 0) bounds.w = 320;
    if (bounds.h <= 0) bounds.h = 120;

    window = XCreateWindow(display, root, bounds.x, bounds.y, (unsigned int)bounds.w, (unsigned int)bounds.h,
                           0, depth, InputOutput, visual, mask, &attrs);
    if (!window) {
        XCloseDisplay(display);
        return 0;
    }

    XStoreName(display, window, title ? title : "zcsr");
    zcsr_set_atom(display, window, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK");
    zcsr_set_atom(display, window, "_NET_WM_STATE", "_NET_WM_STATE_ABOVE");
    XMapRaised(display, window);
    XFlush(display);

    /* Select focus events (merge with any existing event mask). */
    {
        XWindowAttributes wa;
        if (XGetWindowAttributes(display, window, &wa)) {
            XSelectInput(display, window, wa.your_event_mask | FocusChangeMask);
        }
    }

    surface = zcsr_surface_acquire();
    if (!surface) {
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        return 0;
    }

    surface->display = display;
    surface->window = window;
    surface->xsettings_owner = None;
    surface->xsettings_selection_atom =
        XInternAtom(display, "_XSETTINGS_S0", False);
    surface->xsettings_settings_atom =
        XInternAtom(display, "_XSETTINGS_SETTINGS", False);
    XSelectInput(display, root, PropertyChangeMask);
#if defined(ZCSR_HAVE_XRANDR)
    {
        int event_base = 0;
        int error_base = 0;
        if (XRRQueryExtension(display, &event_base, &error_base)) {
            XRRSelectInput(display, root, RRScreenChangeNotifyMask);
        }
    }
#endif
    (void)zcsr_linux_refresh_xsettings_owner(surface);
    surface->dpi_scale = zcsr_surface_get_dpi_scale(surface);
    if (!isfinite(surface->dpi_scale) || surface->dpi_scale <= 0.0f) {
        surface->dpi_scale = 1.0f;
    }
    return surface;
}

void zcsr_surface_destroy(zcsr_surface* s) {
    if (!s || !s->in_use) return;
    if (s->display && s->window) {
        XDestroyWindow((Display*)s->display, (Window)s->window);
        XCloseDisplay((Display*)s->display);
    }
    zcsr_surface_release(s);
}

void* zcsr_surface_native_handle(const zcsr_surface* s) {
    return (s && s->in_use) ? (void*)(uintptr_t)s->window : 0;
}

void* zcsr_surface_native_display(const zcsr_surface* s) {
    return (s && s->in_use) ? s->display : 0; /* the X11 Display*, reusable by renderers */
}

void zcsr_surface_pump_events(zcsr_surface* s) {
    Display* display;
    Window root;
    bool dpi_may_have_changed = false;
    bool owner_changed;
    if (!s || !s->in_use || !s->display) return;
    display = (Display*)s->display;
    root = RootWindow(display, DefaultScreen(display));
    owner_changed = zcsr_linux_refresh_xsettings_owner(s);
    if (owner_changed) dpi_may_have_changed = true;
    while (XPending(display) > 0) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == PropertyNotify) {
            XPropertyEvent* property_event = &event.xproperty;
            if (property_event->window == root &&
                property_event->atom == XA_RESOURCE_MANAGER) {
                dpi_may_have_changed = true;
            }
            if (s->xsettings_owner != None &&
                property_event->window == s->xsettings_owner &&
                property_event->atom == s->xsettings_settings_atom) {
                dpi_may_have_changed = true;
            }
        }
        if (event.type == FocusIn &&
            event.xfocus.window == (Window)s->window) {
            zcsr_surface_dispatch_focus(s, ZCSR_FOCUS_GAINED);
        }
        if (event.type == FocusOut &&
            event.xfocus.window == (Window)s->window) {
            zcsr_surface_dispatch_focus(s, ZCSR_FOCUS_LOST);
        }
#if defined(ZCSR_HAVE_XRANDR)
        {
            int event_base = 0;
            int error_base = 0;
            if (XRRQueryExtension(display, &event_base, &error_base) &&
                event.type == event_base + RRScreenChangeNotify) {
                XRRUpdateConfiguration(&event);
                dpi_may_have_changed = true;
            }
        }
#endif
    }
    if (zcsr_linux_refresh_xsettings_owner(s)) {
        dpi_may_have_changed = true;
    }
    if (dpi_may_have_changed) {
        float new_scale = zcsr_surface_get_dpi_scale(s);
        zcsr_surface_update_dpi_scale(s, new_scale);
    }
}

static void zcsr_linux_apply_size_hints(zcsr_surface* s) {
    Display* dpy;
    XSizeHints* hints;
    long supplied = 0;
    if (!s || !s->in_use || !s->display || !s->window) return;
    dpy = (Display*)s->display;
    hints = XAllocSizeHints();
    if (!hints) return;
    if (XGetWMNormalHints(dpy, (Window)s->window, hints, &supplied)) {
        hints->flags &= ~(PMinSize | PMaxSize);
    } else {
        hints->flags = 0;
    }
    if (s->min_w > 0 || s->min_h > 0) {
        hints->min_width = s->min_w > 0 ? s->min_w : 1;
        hints->min_height = s->min_h > 0 ? s->min_h : 1;
        hints->flags |= PMinSize;
    }
    if (s->max_w > 0 || s->max_h > 0) {
        hints->max_width = s->max_w > 0 ? s->max_w : INT_MAX;
        hints->max_height = s->max_h > 0 ? s->max_h : INT_MAX;
        hints->flags |= PMaxSize;
    }
    XSetWMNormalHints(dpy, (Window)s->window, hints);
    XFree(hints);
}

static void zcsr_surface_apply_size_limits(zcsr_surface* s) {
    if (!s || !s->display || !s->window) return;
    zcsr_linux_apply_size_hints(s);
}

#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

static void zcsr_surface_apply_size_limits(zcsr_surface* s) {
    if (!s || !s->hwnd) return;
    /* Force the system to re-query WM_GETMINMAXINFO with new limits. */
    SetWindowPos((HWND)s->hwnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void zcsr_win_client_to_window(HWND hwnd, int client_w, int client_h,
                                       int* win_w, int* win_h) {
    RECT r = {0, 0, client_w, client_h};
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    BOOL ok = FALSE;
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *adjust_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);
        adjust_fn p = (adjust_fn)(void*)GetProcAddress(user32, "AdjustWindowRectExForDpi");
        if (p) {
            typedef UINT (WINAPI *get_dpi_fn)(HWND);
            get_dpi_fn gp = (get_dpi_fn)(void*)GetProcAddress(user32, "GetDpiForWindow");
            UINT dpi = gp ? gp(hwnd) : 96;
            if (dpi == 0) dpi = 96;
            ok = p(&r, style, FALSE, exstyle, dpi);
        }
    }
    if (!ok) AdjustWindowRectEx(&r, style, FALSE, exstyle);
    if (win_w) *win_w = r.right - r.left;
    if (win_h) *win_h = r.bottom - r.top;
}

static zcsr_surface* zcsr_surface_find_by_hwnd(HWND hwnd) {
    size_t i;
    if (!hwnd) return NULL;
    for (i = 0; i < ZCSR_SURFACE_POOL; ++i) {
        zcsr_surface* s = &zcsr_surfaces[i];
        if (s->in_use && s->hwnd == (void*)hwnd) {
            return s;
        }
    }
    return NULL;
}

static LRESULT CALLBACK zcsr_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (msg == WM_DPICHANGED) {
        zcsr_surface* s = zcsr_surface_find_by_hwnd(hwnd);
        const RECT* suggested = (const RECT*)lp;
        UINT dpi = HIWORD(wp);
        if (s && dpi > 0) {
            if (s->mode != ZCSR_WINDOW_FULLSCREEN && suggested) {
                SetWindowPos(
                    hwnd,
                    NULL,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
            }
            zcsr_surface_update_dpi_scale(s, (float)dpi / 96.0f);
        }
        return 0;
    }
    if (msg == WM_GETMINMAXINFO) {
        zcsr_surface* s = zcsr_surface_find_by_hwnd(hwnd);
        if (s && (s->min_w > 0 || s->min_h > 0 || s->max_w > 0 || s->max_h > 0)) {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            if (s->min_w > 0 || s->min_h > 0) {
                int ww, wh;
                zcsr_win_client_to_window(hwnd,
                    s->min_w > 0 ? s->min_w : 0,
                    s->min_h > 0 ? s->min_h : 0, &ww, &wh);
                if (s->min_w > 0) mmi->ptMinTrackSize.x = ww;
                if (s->min_h > 0) mmi->ptMinTrackSize.y = wh;
            }
            if (s->max_w > 0 || s->max_h > 0) {
                int ww, wh;
                zcsr_win_client_to_window(hwnd,
                    s->max_w > 0 ? s->max_w : 0,
                    s->max_h > 0 ? s->max_h : 0, &ww, &wh);
                if (s->max_w > 0) mmi->ptMaxTrackSize.x = ww;
                if (s->max_h > 0) mmi->ptMaxTrackSize.y = wh;
            }
        }
        return 0;
    }
    if (msg == WM_SETFOCUS) {
        LRESULT result = DefWindowProcA(hwnd, msg, wp, lp);
        zcsr_surface* s = zcsr_surface_find_by_hwnd(hwnd);
        if (s) zcsr_surface_dispatch_focus(s, ZCSR_FOCUS_GAINED);
        return result;
    }
    if (msg == WM_KILLFOCUS) {
        LRESULT result = DefWindowProcA(hwnd, msg, wp, lp);
        zcsr_surface* s = zcsr_surface_find_by_hwnd(hwnd);
        if (s) zcsr_surface_dispatch_focus(s, ZCSR_FOCUS_LOST);
        return result;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static ATOM zcsr_window_class(void) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSA wc = { 0 };
        wc.lpfnWndProc = zcsr_wndproc;
        wc.hInstance = GetModuleHandleA(0);
        wc.lpszClassName = "zcsr_overlay_surface";
        atom = RegisterClassA(&wc);
    }
    return atom;
}

zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) {
    HWND hwnd;
    zcsr_surface* surface;
    if (!zcsr_window_class()) return 0;
    if (bounds.w <= 0) bounds.w = 320;
    if (bounds.h <= 0) bounds.h = 120;
    hwnd = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                           "zcsr_overlay_surface", title ? title : "zcsr", WS_POPUP,
                           bounds.x, bounds.y, bounds.w, bounds.h,
                           0, 0, GetModuleHandleA(0), 0);
    if (!hwnd) return 0;
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    surface = zcsr_surface_acquire();
    if (!surface) {
        DestroyWindow(hwnd);
        return 0;
    }
    surface->hwnd = hwnd;
    surface->dpi_scale = zcsr_surface_get_dpi_scale(surface);
    if (!isfinite(surface->dpi_scale) || surface->dpi_scale <= 0.0f) {
        surface->dpi_scale = 1.0f;
    }
    return surface;
}

void zcsr_surface_destroy(zcsr_surface* s) {
    if (!s || !s->in_use) return;
    if (s->hwnd) DestroyWindow((HWND)s->hwnd);
    zcsr_surface_release(s);
}

void* zcsr_surface_native_handle(const zcsr_surface* s) {
    return (s && s->in_use) ? s->hwnd : 0;
}

void* zcsr_surface_native_display(const zcsr_surface* s) {
    (void)s; return 0; /* Win32 has no separate display connection; the HWND suffices */
}

void zcsr_surface_pump_events(zcsr_surface* s) {
    MSG msg;
    (void)s;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

#elif defined(__APPLE__)
/* Cocoa lives in platform_cocoa.m (Objective-C). This C side keeps the shared pool/struct and
 * delegates the NSWindow work through a thin C-ABI shim — no Objective-C leaks into the contract. */
#include "platform_cocoa.h"

zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) {
    void* window;
    zcsr_surface* surface;
    if (bounds.w <= 0) bounds.w = 320;
    if (bounds.h <= 0) bounds.h = 120;
    window = zcsr_cocoa_window_create(title ? title : "zcsr", bounds.x, bounds.y, bounds.w, bounds.h);
    if (!window) return 0;
    surface = zcsr_surface_acquire();
    if (!surface) {
        zcsr_cocoa_window_destroy(window);
        return 0;
    }
    surface->ns_window = window;
    surface->dpi_scale = zcsr_surface_get_dpi_scale(surface);
    if (!isfinite(surface->dpi_scale) || surface->dpi_scale <= 0.0f) {
        surface->dpi_scale = 1.0f;
    }
    return surface;
}

void zcsr_surface_destroy(zcsr_surface* s) {
    if (!s || !s->in_use) return;
    if (s->ns_window) zcsr_cocoa_window_destroy(s->ns_window);
    zcsr_surface_release(s);
}

void* zcsr_surface_native_handle(const zcsr_surface* s) {
    return (s && s->in_use) ? s->ns_window : 0; /* the NSWindow* */
}

void* zcsr_surface_native_display(const zcsr_surface* s) {
    (void)s; return 0; /* Cocoa has no separate display connection; the NSWindow* suffices */
}

void zcsr_surface_pump_events(zcsr_surface* s) {
    if (s && s->in_use && s->ns_window) zcsr_cocoa_window_pump(s->ns_window);
}

#else
zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) { (void)title; (void)bounds; return 0; }
void          zcsr_surface_destroy(zcsr_surface* s)                    { zcsr_surface_release(s); }
void*         zcsr_surface_native_handle(const zcsr_surface* s)        { (void)s; return 0; }
void*         zcsr_surface_native_display(const zcsr_surface* s)       { (void)s; return 0; }
void          zcsr_surface_pump_events(zcsr_surface* s)                { (void)s; }
#endif

bool zcsr_surface_attach_native(zcsr_surface* s, void* native_window) {
#if defined(__ANDROID__)
    ANativeWindow* next;
    ANativeWindow* previous;
    if (!s || !s->in_use || !native_window) return false;
    next = (ANativeWindow*)native_window;
    previous = (ANativeWindow*)s->native_window;
    /* Reattaching the same window must not acquire another reference. */
    if (previous == next) return true;
    /* Acquire first so replacement cannot drop the old reference too early. */
    ANativeWindow_acquire(next);
    s->native_window = next;
    if (previous) {
        ANativeWindow_release(previous);
    }
    return true;
#else
    (void)s;
    (void)native_window;
    return false;
#endif
}

void zcsr_surface_detach_native(zcsr_surface* s) {
#if defined(__ANDROID__)
    ANativeWindow* previous;
    if (!s || !s->in_use) return;
    previous = (ANativeWindow*)s->native_window;
    s->native_window = 0;
    if (previous) {
        ANativeWindow_release(previous);
    }
#else
    (void)s;
#endif
}

/* ===================================================================== */
/* A1 — display enumeration and window management                         */
/* ===================================================================== */

#include <string.h>

/* Copy at most cap-1 bytes, always NUL-terminated. */
static void zcsr_copy_name(char* dst, size_t cap, const char* src) {
    size_t i = 0;
    if (src) {
        for (; i + 1 < cap && src[i] != '\0'; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/* Copy at most sizeof(out->name)-1 bytes, always NUL-terminated. */
static void zcsr_display_set_name(zcsr_display_info* out, const char* name) {
    zcsr_copy_name(out->name, sizeof(out->name), name);
}

/* A conservative "unknown single display" fallback. Width/height/refresh are
 * 0 (unknown), dpi is 1.0; never fabricate a resolution or refresh rate. */
static zcsr_display_info zcsr_unknown_display(int id) {
    zcsr_display_info info;
    memset(&info, 0, sizeof(info));
    info.id = id;
    info.dpi_scale = 1.0f;
    info.is_primary = 1;
    zcsr_display_set_name(&info, "Unknown");
    return info;
}

#if defined(__ANDROID__)
/* Android compatibility path. There is no desktop/multi-monitor concept, and
 * the API cannot modify window mode/size/position.
 * display_get has no surface parameter, so it cannot query the ANativeWindow
 * size: it returns a conservative fallback. Real window client size is
 * available through zcsr_surface_get_client_size(surface,...). */
int zcsr_display_count(void) {
    return 1;
}
bool zcsr_display_get(int index, zcsr_display_info* out) {
    if (!out || index != 0) return false;
    zcsr_display_info info = zcsr_unknown_display(0);
    zcsr_display_set_name(&info, "Android");
    *out = info;
    return true;
}
bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    (void)s; (void)mode;
    return false;
}
bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    (void)s; (void)w; (void)h;
    return false;
}
bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    (void)s; (void)display_id;
    return false;
}
void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    if (!s || !s->in_use || !s->native_window) return;
    ANativeWindow* nw = (ANativeWindow*)s->native_window;
    if (w) *w = ANativeWindow_getWidth(nw);
    if (h) *h = ANativeWindow_getHeight(nw);
}
float zcsr_surface_get_dpi_scale(const zcsr_surface* s) {
    (void)s;
    return 1.0f; /* cannot reliably query without JNI; A2 territory */
}

#elif defined(__APPLE__)
/* macOS placeholder — real AppKit implementation is deferred to the B group.
 * It compiles, never fabricates data, and refuses all window mutations. */
int zcsr_display_count(void) {
    return 1;
}
bool zcsr_display_get(int index, zcsr_display_info* out) {
    if (!out || index != 0) return false;
    *out = zcsr_unknown_display(0);
    return true;
}
bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    (void)s; (void)mode; return false;
}
bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    (void)s; (void)w; (void)h; return false;
}
bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    (void)s; (void)display_id; return false;
}
void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    (void)s; (void)w; (void)h;
}
float zcsr_surface_get_dpi_scale(const zcsr_surface* s) {
    (void)s; return 1.0f;
}

#elif defined(_WIN32)
/* ============================ Windows ============================== */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef ZCSR_DISPLAY_MAX
#define ZCSR_DISPLAY_MAX 8
#endif

/* Stable display-id table. The public id is the slot index; slots are matched
 * by HMONITOR first so a still-connected display keeps its id. Disconnected
 * slots become tombstones (valid=false) and are never silently reassigned to a
 * different monitor. HMONITOR is never truncated into an int. */
static struct {
    HMONITOR mon;
    bool     ever_used;
    bool     valid;
} zcsr_win_mon_table[ZCSR_DISPLAY_MAX];

typedef struct {
    HMONITOR       mon[ZCSR_DISPLAY_MAX];
    MONITORINFOEXA info[ZCSR_DISPLAY_MAX];
    int            count;
} zcsr_enum_ctx;

static BOOL CALLBACK zcsr_monitor_enum_proc(HMONITOR mon, HDC hdc, LPRECT rc, LPARAM lp) {
    zcsr_enum_ctx* ctx = (zcsr_enum_ctx*)lp;
    (void)hdc; (void)rc;
    if (ctx->count >= ZCSR_DISPLAY_MAX) return TRUE;
    MONITORINFOEXA mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(mon, (LPMONITORINFO)&mi)) {
        ctx->mon[ctx->count] = mon;
        ctx->info[ctx->count] = mi;
        ctx->count++;
    }
    return TRUE;
}

/* Dynamically loaded per-monitor DPI (avoids a hard shcore dependency; when
 * unavailable or the process is not DPI-aware, falls back to 1.0). */
typedef HRESULT (WINAPI *zcsr_get_dpi_for_monitor_fn)(HMONITOR, int, UINT*, UINT*);
#define ZCSR_MDT_EFFECTIVE_DPI 0
static float zcsr_win_monitor_dpi(HMONITOR mon) {
    HMODULE shcore = LoadLibraryA("shcore.dll");
    float scale = 1.0f;
    if (shcore) {
        zcsr_get_dpi_for_monitor_fn p =
            (zcsr_get_dpi_for_monitor_fn)(void*)GetProcAddress(shcore, "GetDpiForMonitor");
        if (p) {
            UINT dx = 96, dy = 96;
            if (p(mon, ZCSR_MDT_EFFECTIVE_DPI, &dx, &dy) == 0 && dx > 0) {
                scale = (float)dx / 96.0f;
            }
        }
        FreeLibrary(shcore);
    }
    return scale;
}

static void zcsr_win_refresh_displays(void) {
    zcsr_enum_ctx ctx;
    bool seen[ZCSR_DISPLAY_MAX];
    int i, s;
    memset(&ctx, 0, sizeof(ctx));
    memset(seen, 0, sizeof(seen));
    EnumDisplayMonitors(NULL, NULL, zcsr_monitor_enum_proc, (LPARAM)&ctx);

    for (i = 0; i < ctx.count; ++i) {
        int slot = -1;
        for (s = 0; s < ZCSR_DISPLAY_MAX; ++s) {
            if (zcsr_win_mon_table[s].ever_used &&
                zcsr_win_mon_table[s].valid &&
                zcsr_win_mon_table[s].mon == ctx.mon[i]) {
                slot = s;
                break;
            }
        }
        if (slot < 0) {
            /* Allocate a never-used slot; never reuse a tombstone. */
            for (s = 0; s < ZCSR_DISPLAY_MAX; ++s) {
                if (!zcsr_win_mon_table[s].ever_used) { slot = s; break; }
            }
        }
        if (slot < 0) break; /* table exhausted */
        zcsr_win_mon_table[slot].mon = ctx.mon[i];
        zcsr_win_mon_table[slot].ever_used = true;
        zcsr_win_mon_table[slot].valid = true;
        seen[slot] = true;
    }
    for (s = 0; s < ZCSR_DISPLAY_MAX; ++s) {
        if (zcsr_win_mon_table[s].ever_used && !seen[s]) {
            zcsr_win_mon_table[s].valid = false; /* tombstone */
        }
    }
}

static int zcsr_win_index_to_slot(int index) {
    int s, n = 0;
    if (index < 0) return -1;
    for (s = 0; s < ZCSR_DISPLAY_MAX; ++s) {
        if (zcsr_win_mon_table[s].valid) {
            if (n == index) return s;
            n++;
        }
    }
    return -1;
}

static bool zcsr_win_fill_info(int slot, zcsr_display_info* out) {
    HMONITOR mon = zcsr_win_mon_table[slot].mon;
    MONITORINFOEXA mi;
    DEVMODEA dm;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, (LPMONITORINFO)&mi)) return false;
    memset(out, 0, sizeof(*out));
    out->id = slot;
    out->x = mi.rcMonitor.left;
    out->y = mi.rcMonitor.top;
    out->width = mi.rcMonitor.right - mi.rcMonitor.left;
    out->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    out->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    out->dpi_scale = zcsr_win_monitor_dpi(mon);
    zcsr_display_set_name(out, mi.szDevice);
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
        (dm.dmFields & DM_DISPLAYFREQUENCY)) {
        out->refresh_hz = (int)dm.dmDisplayFrequency;
    }
    return true;
}

int zcsr_display_count(void) {
    int s, n = 0;
    zcsr_win_refresh_displays();
    for (s = 0; s < ZCSR_DISPLAY_MAX; ++s) {
        if (zcsr_win_mon_table[s].valid) n++;
    }
    return n > 0 ? n : 1;
}

bool zcsr_display_get(int index, zcsr_display_info* out) {
    int slot;
    if (!out) return false;
    zcsr_win_refresh_displays();
    slot = zcsr_win_index_to_slot(index);
    if (slot < 0) return false;
    return zcsr_win_fill_info(slot, out);
}

/* Resolve a stable display id to a current HMONITOR and its monitor rect. */
static HMONITOR zcsr_win_id_to_monitor(int display_id, RECT* rc) {
    MONITORINFO mi;
    if (display_id < 0 || display_id >= ZCSR_DISPLAY_MAX) return NULL;
    if (!zcsr_win_mon_table[display_id].valid) return NULL;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(zcsr_win_mon_table[display_id].mon, &mi)) return NULL;
    if (rc) *rc = mi.rcMonitor;
    return zcsr_win_mon_table[display_id].mon;
}

static int zcsr_win_monitor_id_for_window(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    int s;
    zcsr_win_refresh_displays();
    for (s = 0; s < ZCSR_DISPLAY_MAX; ++s) {
        if (zcsr_win_mon_table[s].valid && zcsr_win_mon_table[s].mon == mon) {
            return s;
        }
    }
    return 0;
}

static const LONG ZCSR_WIN_STYLE_WINDOWED =
    WS_OVERLAPPEDWINDOW | WS_VISIBLE;
static const LONG ZCSR_WIN_STYLE_BORDERLESS =
    WS_POPUP | WS_VISIBLE;
static const LONG ZCSR_WIN_EX_NORMAL = 0;
static const LONG ZCSR_WIN_EX_OVERLAY =
    WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

static void zcsr_win_apply_geometry(HWND hwnd, int x, int y, int w, int h) {
    SetWindowPos(hwnd, NULL, x, y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
}

bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    HWND hwnd;
    LONG style, exstyle;
    RECT rc;
    if (!s || !s->in_use || !s->hwnd) return false;
    if (mode < ZCSR_WINDOW_WINDOWED || mode > ZCSR_WINDOW_FULLSCREEN) return false;
    hwnd = (HWND)s->hwnd;

    if (mode == s->mode) {
        /* Re-requesting fullscreen must not overwrite the restore snapshot. */
        if (mode == ZCSR_WINDOW_FULLSCREEN && s->fs_valid) return true;
        return true;
    }

    if (mode == ZCSR_WINDOW_FULLSCREEN) {
        MONITORINFO mi;
        HMONITOR mon;
        if (s->fs_valid) return false; /* defensive */
        GetWindowRect(hwnd, &rc);
        s->fs_x = rc.left; s->fs_y = rc.top;
        s->fs_w = rc.right - rc.left; s->fs_h = rc.bottom - rc.top;
        s->fs_style = GetWindowLongA(hwnd, GWL_STYLE);
        s->fs_exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
        s->fs_display = zcsr_win_monitor_id_for_window(hwnd);

        mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoA(mon, &mi)) {
            return false;
        }
        SetWindowLongA(hwnd, GWL_STYLE, ZCSR_WIN_STYLE_BORDERLESS);
        SetWindowLongA(hwnd, GWL_EXSTYLE, ZCSR_WIN_EX_NORMAL);
        SetWindowPos(hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        s->mode = ZCSR_WINDOW_FULLSCREEN;
        s->fs_valid = true;
        return true;
    }

    /* Leaving fullscreen (or switching between the two windowed modes). */
    if (s->mode == ZCSR_WINDOW_FULLSCREEN && s->fs_valid) {
        int target_display = s->fs_display;
        RECT mon_rc;
        s->fs_valid = false;
        s->mode = mode; /* target mode decided by caller */
        style = (mode == ZCSR_WINDOW_WINDOWED)
                    ? ZCSR_WIN_STYLE_WINDOWED
                    : ZCSR_WIN_STYLE_BORDERLESS;
        exstyle = (mode == ZCSR_WINDOW_WINDOWED)
                      ? ZCSR_WIN_EX_NORMAL
                      : ZCSR_WIN_EX_OVERLAY;
        SetWindowLongA(hwnd, GWL_STYLE, style);
        SetWindowLongA(hwnd, GWL_EXSTYLE, exstyle);
        /* If the original display is gone, restore onto the primary monitor. */
        if (!zcsr_win_id_to_monitor(target_display, &mon_rc)) {
            MONITORINFO primary;
            memset(&primary, 0, sizeof(primary));
            primary.cbSize = sizeof(primary);
            if (GetMonitorInfoA(NULL, &primary)) {
                s->fs_x = primary.rcMonitor.left;
                s->fs_y = primary.rcMonitor.top;
            }
        }
        zcsr_win_apply_geometry(hwnd, s->fs_x, s->fs_y, s->fs_w, s->fs_h);
        return true;
    }

    /* Plain windowed <-> borderless switch (no fullscreen involved). */
    s->mode = mode;
    if (mode == ZCSR_WINDOW_WINDOWED) {
        SetWindowLongA(hwnd, GWL_STYLE, ZCSR_WIN_STYLE_WINDOWED);
        SetWindowLongA(hwnd, GWL_EXSTYLE, ZCSR_WIN_EX_NORMAL);
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, ZCSR_WIN_STYLE_BORDERLESS);
        SetWindowLongA(hwnd, GWL_EXSTYLE, ZCSR_WIN_EX_OVERLAY);
    }
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    HWND hwnd;
    RECT rc, client;
    LONG style, exstyle;
    int frame_x, frame_y, win_w, win_h;
    if (!s || !s->in_use || !s->hwnd) return false;
    if (w <= 0 || h <= 0) return false;
    if (s->mode == ZCSR_WINDOW_FULLSCREEN) return false;
    hwnd = (HWND)s->hwnd;
    style = GetWindowLongA(hwnd, GWL_STYLE);
    exstyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    if (!AdjustWindowRectEx(&rc, (DWORD)style, FALSE, (DWORD)exstyle)) {
        return false;
    }
    frame_x = (rc.right - rc.left) - w;
    frame_y = (rc.bottom - rc.top) - h;
    win_w = w + frame_x;
    win_h = h + frame_y;
    if (!GetWindowRect(hwnd, &client)) return false;
    if (!SetWindowPos(hwnd, NULL, client.left, client.top, win_w, win_h,
                      SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER)) {
        return false;
    }
    return true;
}

bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    HWND hwnd;
    RECT target;
    HMONITOR mon;
    if (!s || !s->in_use || !s->hwnd) return false;
    hwnd = (HWND)s->hwnd;
    zcsr_win_refresh_displays();
    mon = zcsr_win_id_to_monitor(display_id, &target);
    if (!mon) return false; /* invalid/disconnected id: do not move */

    if (s->mode == ZCSR_WINDOW_FULLSCREEN) {
        /* Keep fullscreen on the target display; preserve the restore snapshot. */
        SetWindowPos(hwnd, HWND_TOP,
                     target.left, target.top,
                     target.right - target.left,
                     target.bottom - target.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        return true;
    }

    {
        WINDOWINFO wi;
        int win_w, win_h;
        memset(&wi, 0, sizeof(wi));
        wi.cbSize = sizeof(wi);
        if (!GetWindowInfo(hwnd, &wi)) return false;
        win_w = wi.rcWindow.right - wi.rcWindow.left;
        win_h = wi.rcWindow.bottom - wi.rcWindow.top;
        SetWindowPos(hwnd, NULL,
                     target.left, target.top, win_w, win_h,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
    }
    return true;
}

void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    RECT rc;
    HWND hwnd;
    if (!s || !s->in_use || !s->hwnd) return;
    hwnd = (HWND)s->hwnd;
    if (!GetClientRect(hwnd, &rc)) return;
    if (w) *w = rc.right - rc.left;
    if (h) *h = rc.bottom - rc.top;
}

float zcsr_surface_get_dpi_scale(const zcsr_surface* s) {
    HWND hwnd;
    HMONITOR mon;
    UINT dpi = 0;
    HMODULE user32;
    typedef UINT (WINAPI *get_dpi_for_window_fn)(HWND);
    get_dpi_for_window_fn p;
    if (!s || !s->in_use || !s->hwnd) return 1.0f;
    hwnd = (HWND)s->hwnd;
    (void)mon;
    user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        p = (get_dpi_for_window_fn)(void*)GetProcAddress(user32, "GetDpiForWindow");
        if (p) {
            dpi = p(hwnd);
            if (dpi > 0) return (float)dpi / 96.0f;
        }
    }
    return 1.0f;
}

#elif defined(__linux__)
/* ============================ Linux / X11 ============================== */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#if defined(ZCSR_HAVE_XRANDR)
#include <X11/extensions/Xrandr.h>
#endif
#if defined(ZCSR_HAVE_XINERAMA)
#include <X11/extensions/Xinerama.h>
#endif

#ifndef ZCSR_DISPLAY_MAX
#define ZCSR_DISPLAY_MAX 8
#endif

typedef struct {
    int  x, y, w, h;
    int  is_primary;
    char name[64];
} zcsr_x_monitor;

static int zcsr_x_query_monitors(Display* dpy, int screen, zcsr_x_monitor* out, int max) {
    int count = 0;
    Window root = RootWindow(dpy, screen);
#if defined(ZCSR_HAVE_XRANDR)
    {
        XRRScreenResources* res = XRRGetScreenResources(dpy, root);
        RROutput primary = 0;
        int i;
        if (res) {
            primary = XRRGetOutputPrimary(dpy, root);
            for (i = 0; i < res->noutput && count < max; ++i) {
                XRROutputInfo* oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
                if (oi && oi->connection == RR_Connected && oi->crtc) {
                    XRRCrtcInfo* ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
                    if (ci && ci->width > 0 && ci->height > 0) {
                        memset(&out[count], 0, sizeof(out[count]));
                        out[count].x = ci->x;
                        out[count].y = ci->y;
                        out[count].w = ci->width;
                        out[count].h = ci->height;
                        out[count].is_primary = (res->outputs[i] == primary) ? 1 : 0;
                        if (oi->name) {
                            zcsr_copy_name(out[count].name,
                                           sizeof(out[count].name), oi->name);
                        }
                        count++;
                        XRRFreeCrtcInfo(ci);
                    }
                    if (oi) XRRFreeOutputInfo(oi);
                } else if (oi) {
                    XRRFreeOutputInfo(oi);
                }
            }
            XRRFreeScreenResources(res);
        }
        if (count > 0) {
            int k, has_primary = 0;
            for (k = 0; k < count; ++k) {
                if (out[k].is_primary) has_primary = 1;
            }
            /* Some X servers (e.g. WSLg) report no primary output; designate
             * the first connected output so callers always have a primary. */
            if (!has_primary) out[0].is_primary = 1;
            return count;
        }
    }
#endif
#if defined(ZCSR_HAVE_XINERAMA)
    {
        int event_base, error_base;
        if (XineramaQueryExtension(dpy, &event_base, &error_base) &&
            XineramaIsActive(dpy)) {
            int n;
            XineramaScreenInfo* xs = XineramaQueryScreens(dpy, &n);
            if (xs) {
                int i;
                for (i = 0; i < n && count < max; ++i) {
                    memset(&out[count], 0, sizeof(out[count]));
                    out[count].x = xs[i].x_org;
                    out[count].y = xs[i].y_org;
                    out[count].w = xs[i].width;
                    out[count].h = xs[i].height;
                    out[count].is_primary = (i == 0) ? 1 : 0;
                    zcsr_copy_name(out[count].name,
                                   sizeof(out[count].name), "Xinerama");
                    count++;
                }
                XFree(xs);
            }
        }
        if (count > 0) return count;
    }
#endif
    /* Single-screen fallback. */
    memset(&out[0], 0, sizeof(out[0]));
    out[0].x = 0;
    out[0].y = 0;
    out[0].w = DisplayWidth(dpy, screen);
    out[0].h = DisplayHeight(dpy, screen);
    out[0].is_primary = 1;
    zcsr_copy_name(out[0].name, sizeof(out[0].name), "X11");
    return 1;
}

int zcsr_display_count(void) {
    Display* dpy;
    zcsr_x_monitor mons[ZCSR_DISPLAY_MAX];
    int n;
    dpy = XOpenDisplay(0);
    if (!dpy) return 1;
    n = zcsr_x_query_monitors(dpy, DefaultScreen(dpy), mons, ZCSR_DISPLAY_MAX);
    XCloseDisplay(dpy);
    return n > 0 ? n : 1;
}

bool zcsr_display_get(int index, zcsr_display_info* out) {
    Display* dpy;
    zcsr_x_monitor mons[ZCSR_DISPLAY_MAX];
    int n;
    if (!out || index < 0) return false;
    dpy = XOpenDisplay(0);
    if (!dpy) {
        if (index != 0) return false;
        *out = zcsr_unknown_display(0);
        return true;
    }
    n = zcsr_x_query_monitors(dpy, DefaultScreen(dpy), mons, ZCSR_DISPLAY_MAX);
    XCloseDisplay(dpy);
    if (index >= n) return false;
    memset(out, 0, sizeof(*out));
    out->id = index;
    out->x = mons[index].x;
    out->y = mons[index].y;
    out->width = mons[index].w;
    out->height = mons[index].h;
    out->refresh_hz = 0;            /* not reliably portable across XRandR paths */
    out->dpi_scale = 1.0f;
    out->is_primary = mons[index].is_primary;
    zcsr_display_set_name(out, mons[index].name[0] ? mons[index].name : "X11");
    return true;
}

/* EWMH fullscreen request for window-manager-managed windows. */
static void zcsr_x_set_ewmh_fullscreen(Display* dpy, Window win, int full) {
    XEvent xev;
    Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = full ? 1 : 0; /* _NET_WM_STATE_ADD / REMOVE */
    xev.xclient.data.l[1] = fs;
    xev.xclient.data.l[2] = 0;
    xev.xclient.data.l[3] = 0;
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
}

bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    Display* dpy;
    Window win;
    if (!s || !s->in_use || !s->display) return false;
    if (mode < ZCSR_WINDOW_WINDOWED || mode > ZCSR_WINDOW_FULLSCREEN) return false;
    dpy = (Display*)s->display;
    win = (Window)s->window;

    if (mode == s->mode) {
        if (mode == ZCSR_WINDOW_FULLSCREEN && s->fs_valid) return true;
        return true;
    }

    if (mode == ZCSR_WINDOW_FULLSCREEN) {
        zcsr_x_monitor mons[ZCSR_DISPLAY_MAX];
        int n;
        XWindowAttributes wa;
        if (s->fs_valid) return false;
        XGetWindowAttributes(dpy, win, &wa);
        s->fs_x = wa.x; s->fs_y = wa.y;
        s->fs_w = wa.width; s->fs_h = wa.height;
        s->fs_display = 0;
        /* For override_redirect windows (the existing overlay style), the WM
         * does not manage decoration, so EWMH is ineffective and we resize the
         * window directly to the screen. For WM-managed windows, request EWMH. */
        XGetWindowAttributes(dpy, win, &wa);
        if (wa.override_redirect) {
            n = zcsr_x_query_monitors(dpy, DefaultScreen(dpy), mons,
                                      ZCSR_DISPLAY_MAX);
            if (n > 0) {
                XMoveResizeWindow(dpy, win, mons[0].x, mons[0].y,
                                  mons[0].w, mons[0].h);
            }
        } else {
            zcsr_x_set_ewmh_fullscreen(dpy, win, 1);
        }
        s->fs_valid = true;
        s->mode = ZCSR_WINDOW_FULLSCREEN;
        XFlush(dpy);
        return true;
    }

    if (s->mode == ZCSR_WINDOW_FULLSCREEN && s->fs_valid) {
        XWindowAttributes wa;
        XGetWindowAttributes(dpy, win, &wa);
        if (wa.override_redirect) {
            XMoveResizeWindow(dpy, win, s->fs_x, s->fs_y, s->fs_w, s->fs_h);
        } else {
            zcsr_x_set_ewmh_fullscreen(dpy, win, 0);
        }
        s->fs_valid = false;
        s->mode = mode;
        XFlush(dpy);
        return true;
    }

    /* windowed <-> borderless for non-override windows: EWMH decoration hints
     * are a WM responsibility; the override_redirect overlay is inherently
     * borderless. We record the requested mode and leave existing geometry. */
    s->mode = mode;
    XFlush(dpy);
    return true;
}

bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    Display* dpy;
    XWindowAttributes wa;
    if (!s || !s->in_use || !s->display) return false;
    if (w <= 0 || h <= 0) return false;
    if (s->mode == ZCSR_WINDOW_FULLSCREEN) return false;
    dpy = (Display*)s->display;
    XGetWindowAttributes(dpy, (Window)s->window, &wa);
    XResizeWindow(dpy, (Window)s->window, (unsigned)w, (unsigned)h);
    XFlush(dpy);
    return true;
}

bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    Display* dpy;
    zcsr_x_monitor mons[ZCSR_DISPLAY_MAX];
    int n;
    XWindowAttributes wa;
    if (!s || !s->in_use || !s->display) return false;
    dpy = (Display*)s->display;
    n = zcsr_x_query_monitors(dpy, DefaultScreen(dpy), mons, ZCSR_DISPLAY_MAX);
    if (display_id < 0 || display_id >= n) return false;
    XGetWindowAttributes(dpy, (Window)s->window, &wa);
    if (s->mode == ZCSR_WINDOW_FULLSCREEN) {
        XMoveResizeWindow(dpy, (Window)s->window,
                          mons[display_id].x, mons[display_id].y,
                          mons[display_id].w, mons[display_id].h);
        /* keep restore snapshot (fs_x/fs_y/fs_w/fs_h) unchanged */
    } else {
        XMoveWindow(dpy, (Window)s->window,
                    mons[display_id].x, mons[display_id].y);
    }
    XFlush(dpy);
    return true;
}

void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    Display* dpy;
    XWindowAttributes wa;
    if (!s || !s->in_use || !s->display) return;
    dpy = (Display*)s->display;
    if (XGetWindowAttributes(dpy, (Window)s->window, &wa) == 0) return;
    if (w) *w = wa.width;
    if (h) *h = wa.height;
}

static float zcsr_xft_dpi(Display* dpy) {
    Window root;
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* property = NULL;
    int status;
    float result = 0.0f;
    const char* p;
    const char* end;
    if (!dpy) return 0.0f;
    root = RootWindow(dpy, DefaultScreen(dpy));
    status = XGetWindowProperty(
        dpy, root, XA_RESOURCE_MANAGER, 0, 65536, False, AnyPropertyType,
        &actual_type, &actual_format, &nitems, &bytes_after, &property
    );
    if (status != Success || !property ||
        actual_format != 8 || nitems == 0) {
        if (property) XFree(property);
        return 0.0f;
    }
    p = (const char*)property;
    end = p + nitems;
    while (p < end) {
        const char* line_end = p;
        const char* key;
        char* endptr;
        double value;
        while (line_end < end && *line_end != '\n') {
            ++line_end;
        }
        key = p;
        while (key < line_end && isspace((unsigned char)*key)) {
            ++key;
        }
        if ((size_t)(line_end - key) >= 7 &&
            memcmp(key, "Xft.dpi", 7) == 0) {
            const char* q = key + 7;
            while (q < line_end && isspace((unsigned char)*q)) ++q;
            if (q < line_end && *q == ':') {
                char buffer[64];
                size_t len;
                ++q;
                while (q < line_end && isspace((unsigned char)*q)) ++q;
                len = (size_t)(line_end - q);
                if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
                memcpy(buffer, q, len);
                buffer[len] = '\0';
                errno = 0;
                value = strtod(buffer, &endptr);
                if (endptr != buffer && errno != ERANGE &&
                    value > 0.0 && isfinite(value)) {
                    const char* tail = endptr;
                    while (*tail != '\0' && isspace((unsigned char)*tail)) {
                        ++tail;
                    }
                    if (*tail == '\0') {
                        result = (float)value;
                    }
                }
                break;
            }
        }
        p = line_end < end ? line_end + 1 : end;
    }
    XFree(property);
    return result;
}

static uint16_t zcsr_xsettings_u16(const unsigned char* p, bool little_endian) {
    if (little_endian) return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t zcsr_xsettings_u32(const unsigned char* p, bool little_endian) {
    if (little_endian) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
             | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t zcsr_xsettings_pad4(size_t n) {
    return (n + 3U) & ~(size_t)3U;
}

static float zcsr_xsettings_dpi(Display* dpy) {
    Window owner;
    Atom selection_atom;
    Atom settings_atom;
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = NULL;
    int status;
    size_t size;
    size_t pos;
    uint32_t count;
    bool little_endian;
    float result = 0.0f;
    if (!dpy) return 0.0f;
    selection_atom = XInternAtom(dpy, "_XSETTINGS_S0", False);
    settings_atom = XInternAtom(dpy, "_XSETTINGS_SETTINGS", False);
    owner = XGetSelectionOwner(dpy, selection_atom);
    if (owner == None) return 0.0f;
    status = XGetWindowProperty(
        dpy, owner, settings_atom, 0, 262144, False, settings_atom,
        &actual_type, &actual_format, &nitems, &bytes_after, &data
    );
    if (status != Success || !data ||
        actual_type != settings_atom || actual_format != 8 ||
        nitems < 12 || bytes_after != 0) {
        if (data) XFree(data);
        return 0.0f;
    }
    size = (size_t)nitems;
    if (data[0] == 0) little_endian = true;
    else if (data[0] == 1) little_endian = false;
    else { XFree(data); return 0.0f; }
    count = zcsr_xsettings_u32(data + 8, little_endian);
    pos = 12;
    if (count > 65536U) { XFree(data); return 0.0f; }
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t type;
        uint16_t name_len;
        uint32_t value;
        size_t name_start;
        size_t serial_pos;
        size_t value_pos;
        if ((uint64_t)pos + 4U > (uint64_t)size) break;
        type = data[pos];
        name_len = zcsr_xsettings_u16(data + pos + 2, little_endian);
        name_start = pos + 4;
        if ((uint64_t)name_start + name_len > (uint64_t)size) break;
        serial_pos = pos + 4 + zcsr_xsettings_pad4(name_len);
        if ((uint64_t)serial_pos + 4U > (uint64_t)size) break;
        value_pos = serial_pos + 4;
        if (type == 0) {
            if ((uint64_t)value_pos + 4U > (uint64_t)size) break;
            value = zcsr_xsettings_u32(data + value_pos, little_endian);
            if (name_len == 7 &&
                memcmp(data + name_start, "Xft/DPI", 7) == 0) {
                float dpi = (float)((int32_t)value) / 1024.0f;
                if (isfinite(dpi) && dpi > 0.0f) result = dpi;
                break;
            }
            pos = value_pos + 4;
        } else if (type == 1) {
            uint32_t string_len;
            size_t aligned_len;
            if ((uint64_t)value_pos + 4U > (uint64_t)size) break;
            string_len = zcsr_xsettings_u32(data + value_pos, little_endian);
            aligned_len = zcsr_xsettings_pad4(string_len);
            if ((uint64_t)value_pos + 4U + aligned_len > (uint64_t)size) break;
            pos = value_pos + 4 + aligned_len;
        } else if (type == 2) {
            if ((uint64_t)value_pos + 8U > (uint64_t)size) break;
            pos = value_pos + 8;
        } else {
            break;
        }
        if (pos > size) break;
    }
    XFree(data);
    return result;
}

#if defined(ZCSR_HAVE_XRANDR)
static float zcsr_randr_surface_dpi(const zcsr_surface* s) {
    Display* dpy;
    Window root;
    Window child = None;
    XWindowAttributes attrs;
    int root_x = 0;
    int root_y = 0;
    XRRScreenResources* resources = NULL;
    float result = 0.0f;
    int i;
    if (!s || !s->in_use || !s->display || !s->window) return 0.0f;
    dpy = (Display*)s->display;
    root = RootWindow(dpy, DefaultScreen(dpy));
    if (!XGetWindowAttributes(dpy, (Window)s->window, &attrs)) return 0.0f;
    if (!XTranslateCoordinates(dpy, (Window)s->window, root,
                                attrs.width / 2, attrs.height / 2,
                                &root_x, &root_y, &child)) return 0.0f;
    resources = XRRGetScreenResources(dpy, root);
    if (!resources) return 0.0f;
    for (i = 0; i < resources->noutput; ++i) {
        XRROutputInfo* output_info = NULL;
        XRRCrtcInfo* crtc_info = NULL;
        RROutput output = resources->outputs[i];
        RRCrtc crtc;
        double dpi;
        output_info = XRRGetOutputInfo(dpy, resources, output);
        if (!output_info) continue;
        if (output_info->connection != RR_Connected ||
            output_info->crtc == None ||
            output_info->mm_width <= 0) {
            XRRFreeOutputInfo(output_info);
            continue;
        }
        crtc = output_info->crtc;
        crtc_info = XRRGetCrtcInfo(dpy, resources, crtc);
        if (crtc_info && crtc_info->width > 0 && crtc_info->height > 0 &&
            root_x >= crtc_info->x && root_y >= crtc_info->y &&
            root_x < crtc_info->x + (int)crtc_info->width &&
            root_y < crtc_info->y + (int)crtc_info->height) {
            dpi = (double)crtc_info->width * 25.4 / (double)output_info->mm_width;
            if (dpi > 0.0 && isfinite(dpi)) result = (float)dpi;
        }
        if (crtc_info) XRRFreeCrtcInfo(crtc_info);
        XRRFreeOutputInfo(output_info);
        if (result > 0.0f) break;
    }
    XRRFreeScreenResources(resources);
    return result;
}
#else
static float zcsr_randr_surface_dpi(const zcsr_surface* s) {
    (void)s;
    return 0.0f;
}
#endif

static bool zcsr_linux_refresh_xsettings_owner(zcsr_surface* s) {
    Display* dpy;
    Window owner;
    if (!s || !s->in_use || !s->display) return false;
    dpy = (Display*)s->display;
    if (s->xsettings_selection_atom == 0) {
        s->xsettings_selection_atom =
            XInternAtom(dpy, "_XSETTINGS_S0", False);
    }
    if (s->xsettings_settings_atom == 0) {
        s->xsettings_settings_atom =
            XInternAtom(dpy, "_XSETTINGS_SETTINGS", False);
    }
    owner = XGetSelectionOwner(dpy, s->xsettings_selection_atom);
    if (owner == s->xsettings_owner) return false;
    if (s->xsettings_owner != None && s->xsettings_owner != owner) {
        XSelectInput(dpy, s->xsettings_owner, NoEventMask);
    }
    s->xsettings_owner = owner;
    if (owner != None) {
        XSelectInput(dpy, owner, PropertyChangeMask);
    }
    return true;
}

float zcsr_surface_get_dpi_scale(const zcsr_surface* s) {
    Display* dpy;
    float dpi;
    if (!s || !s->in_use || !s->display) return 1.0f;
    dpy = (Display*)s->display;
    dpi = zcsr_xft_dpi(dpy);
    if (dpi > 0.0f) return dpi / 96.0f;
    dpi = zcsr_xsettings_dpi(dpy);
    if (dpi > 0.0f) return dpi / 96.0f;
    dpi = zcsr_randr_surface_dpi(s);
    if (dpi > 0.0f) return dpi / 96.0f;
    return 1.0f;
}

#else
/* ====================== Unknown platform stubs ===================== */
int zcsr_display_count(void) { return 1; }
bool zcsr_display_get(int index, zcsr_display_info* out) {
    if (!out || index != 0) return false;
    *out = zcsr_unknown_display(0);
    return true;
}
bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    (void)s; (void)mode; return false;
}
bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    (void)s; (void)w; (void)h; return false;
}
bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    (void)s; (void)display_id; return false;
}
void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    (void)s; (void)w; (void)h;
}
float zcsr_surface_get_dpi_scale(const zcsr_surface* s) {
    (void)s; return 1.0f;
}
#endif
