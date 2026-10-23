/* Agent 5 — Platform abstraction. Win32 / X11 / Cocoa, gated on ZCSR_PLATFORM (zcsr/platform.h).
 * No OpenGL/DirectX/Vulkan. */
#include "zcsr/platform.h"
#include "platform_internal.h"

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

/* zcsr_surface struct, pool, acquire/release, dpi update live in
 * platform_internal.h (shared with per-backend files like platform_wayland.c). */

zcsr_surface zcsr_surfaces[ZCSR_SURFACE_POOL];

zcsr_surface* zcsr_surface_acquire(void) {
    for (size_t i = 0; i < ZCSR_SURFACE_POOL; ++i) {
        if (!zcsr_surfaces[i].in_use) {
            zcsr_surfaces[i].in_use = true;
            return &zcsr_surfaces[i];
        }
    }
    return 0;
}

void zcsr_surface_release(zcsr_surface* s) {
    if (!s) return;
    *s = (zcsr_surface){ 0 };
    s->dpi_scale = 1.0f;
}

void zcsr_surface_update_dpi_scale(
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
void zcsr_surface_apply_size_limits(zcsr_surface* s);
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
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

void zcsr_surface_apply_size_limits(zcsr_surface* s) {
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
    if (msg == WM_NCHITTEST) {
        zcsr_surface* s = zcsr_surface_find_by_hwnd(hwnd);
        if (s && s->drag_region_enabled && s->mode != ZCSR_WINDOW_FULLSCREEN) {
            POINT pt;
            pt.x = (int)(short)LOWORD(lp);
            pt.y = (int)(short)HIWORD(lp);
            ScreenToClient(hwnd, &pt);
            if (pt.x >= s->drag_x && pt.x < s->drag_x + s->drag_w &&
                pt.y >= s->drag_y && pt.y < s->drag_y + s->drag_h) {
                return HTCAPTION;
            }
        }
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
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
    surface = zcsr_surface_acquire();
    if (!surface) {
        DestroyWindow(hwnd);
        return 0;
    }
    surface->hwnd = hwnd;
    surface->mode = ZCSR_WINDOW_BORDERLESS; /* CreateWindowEx uses WS_POPUP + WS_EX_LAYERED overlay */
    /* Stash the requested bounds as the windowed-mode geometry; borderless
     * is always full-screen, so expand to the monitor now. */
    surface->win_valid = true;
    surface->win_x = bounds.x; surface->win_y = bounds.y;
    surface->win_w = bounds.w; surface->win_h = bounds.h;
    {
        MONITORINFO mi;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoA(mon, &mi)) {
            bounds.x = mi.rcMonitor.left;
            bounds.y = mi.rcMonitor.top;
            bounds.w = mi.rcMonitor.right - mi.rcMonitor.left;
            bounds.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
    }
    /* HWND_TOPMOST required so WS_EX_TOPMOST actually places the window
     * above the taskbar; CreateWindowEx alone is not sufficient. */
    SetWindowPos(hwnd, HWND_TOPMOST, bounds.x, bounds.y, bounds.w, bounds.h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

#elif !defined(__linux__)
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
void zcsr_copy_name(char* dst, size_t cap, const char* src) {
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
bool zcsr_surface_set_drag_region(zcsr_surface* s, int x, int y, int w, int h) {
    (void)s; (void)x; (void)y; (void)w; (void)h;
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
bool zcsr_surface_set_drag_region(zcsr_surface* s, int x, int y, int w, int h) {
    (void)s; (void)x; (void)y; (void)w; (void)h; return false;
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

static void zcsr_win_apply_geometry(HWND hwnd, int x, int y, int w, int h, bool topmost) {
    /* WS_EX_TOPMOST requires SetWindowPos with HWND_TOPMOST to take effect;
     * SWP_NOZORDER would leave the window below the taskbar. */
    HWND insert_after = topmost ? HWND_TOPMOST : HWND_TOP;
    SetWindowPos(hwnd, insert_after, x, y, w, h,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
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
        if (mode == ZCSR_WINDOW_BORDERLESS) {
            /* Borderless is always full-screen: use the monitor rect. */
            MONITORINFO mi;
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoA(mon, &mi)) {
                zcsr_win_apply_geometry(hwnd,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    mi.rcMonitor.right - mi.rcMonitor.left,
                    mi.rcMonitor.bottom - mi.rcMonitor.top, true);
            } else {
                zcsr_win_apply_geometry(hwnd, s->fs_x, s->fs_y, s->fs_w, s->fs_h, true);
            }
        } else {
            /* Windowed: restore saved windowed geometry (fallback to fs snapshot). */
            int rx = s->win_valid ? s->win_x : s->fs_x;
            int ry = s->win_valid ? s->win_y : s->fs_y;
            int rw = s->win_valid ? s->win_w : s->fs_w;
            int rh = s->win_valid ? s->win_h : s->fs_h;
            zcsr_win_apply_geometry(hwnd, rx, ry, rw, rh, false);
        }
        return true;
    }

    /* Plain windowed <-> borderless switch (no fullscreen involved).
     * Borderless is always full-screen; windowed restores the saved geometry. */
    if (mode == ZCSR_WINDOW_BORDERLESS) {
        /* Save current windowed geometry before expanding to full-screen. */
        RECT cur;
        GetWindowRect(hwnd, &cur);
        s->win_valid = true;
        s->win_x = cur.left; s->win_y = cur.top;
        s->win_w = cur.right - cur.left; s->win_h = cur.bottom - cur.top;
        /* Expand to monitor full-screen. */
        MONITORINFO mi;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        int fx = 0, fy = 0, fw = 1280, fh = 720;
        if (GetMonitorInfoA(mon, &mi)) {
            fx = mi.rcMonitor.left; fy = mi.rcMonitor.top;
            fw = mi.rcMonitor.right - mi.rcMonitor.left;
            fh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }
        s->mode = mode;
        SetWindowLongA(hwnd, GWL_STYLE, ZCSR_WIN_STYLE_BORDERLESS);
        SetWindowLongA(hwnd, GWL_EXSTYLE, ZCSR_WIN_EX_OVERLAY);
        SetWindowPos(hwnd, HWND_TOPMOST, fx, fy, fw, fh,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        /* Restore saved windowed geometry (or default to centered 1280x720). */
        int rx = s->win_valid ? s->win_x : 100;
        int ry = s->win_valid ? s->win_y : 100;
        int rw = s->win_valid ? s->win_w : 1280;
        int rh = s->win_valid ? s->win_h : 720;
        s->mode = mode;
        SetWindowLongA(hwnd, GWL_STYLE, ZCSR_WIN_STYLE_WINDOWED);
        SetWindowLongA(hwnd, GWL_EXSTYLE, ZCSR_WIN_EX_NORMAL);
        SetWindowPos(hwnd, HWND_NOTOPMOST, rx, ry, rw, rh,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
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

bool zcsr_surface_set_drag_region(zcsr_surface* s, int x, int y, int w, int h) {
    if (!s || !s->in_use || !s->hwnd) return false;
    if (w <= 0 || h <= 0) {
        s->drag_region_enabled = false;
        return true;
    }
    s->drag_x = x;
    s->drag_y = y;
    s->drag_w = w;
    s->drag_h = h;
    s->drag_region_enabled = true;
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

#elif !defined(__linux__)
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
bool zcsr_surface_set_drag_region(zcsr_surface* s, int x, int y, int w, int h) {
    (void)s; (void)x; (void)y; (void)w; (void)h; return false;
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

/* ===========================================================================================
 * Cursor control API (additive)
 * =========================================================================================== */

#if defined(_WIN32)

static HCURSOR g_custom_cursor = NULL;
static bool g_cursor_visible = true;

bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) {
    (void)s;
    g_cursor_visible = visible;
    if (visible) {
        while (ShowCursor(TRUE) < 0) { /* spin until display count >= 0 */ }
    } else {
        while (ShowCursor(FALSE) >= 0) { /* spin until display count < 0 */ }
    }
    return true;
}

bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s,
                                    const unsigned char* rgba, int w, int h,
                                    int hot_x, int hot_y) {
    HDC hdc, mask_dc, color_dc;
    HBITMAP color_bmp, mask_bmp, old_color, old_mask;
    ICONINFO ii;
    HCURSOR new_cursor;
    int x, y;
    (void)s;
    if (!rgba || w <= 0 || h <= 0 || w > 256 || h > 256) {
        zcsr_surface_reset_cursor(s);
        return false;
    }
    hdc = GetDC(NULL);
    color_dc = CreateCompatibleDC(hdc);
    mask_dc = CreateCompatibleDC(hdc);
    color_bmp = CreateCompatibleBitmap(hdc, w, h);
    mask_bmp = CreateBitmap(w, h, 1, 1, NULL);
    old_color = (HBITMAP)SelectObject(color_dc, color_bmp);
    old_mask = (HBITMAP)SelectObject(mask_dc, mask_bmp);
    for (y = 0; y < h; ++y) {
        for (x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4;
            unsigned char r = rgba[idx + 0];
            unsigned char g = rgba[idx + 1];
            unsigned char b = rgba[idx + 2];
            unsigned char a = rgba[idx + 3];
            SetPixelV(color_dc, x, y, RGB(r, g, b));
            SetPixelV(mask_dc, x, y, a > 128 ? RGB(255,255,255) : RGB(0,0,0));
        }
    }
    SelectObject(color_dc, old_color);
    SelectObject(mask_dc, old_mask);
    ii.fIcon = FALSE;
    ii.xHotspot = (DWORD)hot_x;
    ii.yHotspot = (DWORD)hot_y;
    ii.hbmMask = mask_bmp;
    ii.hbmColor = color_bmp;
    new_cursor = CreateIconIndirect(&ii);
    DeleteDC(color_dc);
    DeleteDC(mask_dc);
    DeleteObject(color_bmp);
    DeleteObject(mask_bmp);
    ReleaseDC(NULL, hdc);
    if (!new_cursor) return false;
    if (g_custom_cursor) DestroyCursor(g_custom_cursor);
    g_custom_cursor = new_cursor;
    SetCursor(g_custom_cursor);
    return true;
}

bool zcsr_surface_reset_cursor(zcsr_surface* s) {
    (void)s;
    if (g_custom_cursor) {
        DestroyCursor(g_custom_cursor);
        g_custom_cursor = NULL;
    }
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    if (!g_cursor_visible) {
        g_cursor_visible = true;
        while (ShowCursor(TRUE) < 0) { }
    }
    return true;
}

#elif defined(__ANDROID__)

/* Android has no mouse cursor in most configurations — return false. */
bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) { (void)s; (void)visible; return false; }
bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s, const unsigned char* rgba, int w, int h,
                                    int hot_x, int hot_y) { (void)s; (void)rgba; (void)w; (void)h; (void)hot_x; (void)hot_y; return false; }
bool zcsr_surface_reset_cursor(zcsr_surface* s) { (void)s; return false; }

#elif !defined(__linux__)

/* macOS stub — return false (first release no RGBA cursor support). */
bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) { (void)s; (void)visible; return false; }
bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s, const unsigned char* rgba, int w, int h,
                                    int hot_x, int hot_y) { (void)s; (void)rgba; (void)w; (void)h; (void)hot_x; (void)hot_y; return false; }
bool zcsr_surface_reset_cursor(zcsr_surface* s) { (void)s; return false; }

#endif
