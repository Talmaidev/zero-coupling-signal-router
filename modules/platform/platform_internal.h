#ifndef ZCSR_PLATFORM_INTERNAL_H
#define ZCSR_PLATFORM_INTERNAL_H
/* Module-private shared definitions for the platform module.
 * Included only by translation units under modules/platform/.
 * The zero-coupling guard bans "../" and "modules/" includes;
 * a same-directory internal header is allowed and never leaves the module. */

#include "zcsr/platform.h"
#include <stdbool.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Opaque surface — laid out here so both platform.c and per-backend */
/* files see the same fields.  Everything lives in a fixed pool      */
/* (zero heap: no malloc/calloc in zcsr code).                        */
/* ------------------------------------------------------------------ */
struct zcsr_surface {
    /* ---- common state ---- */
    bool in_use;
    float dpi_scale;
    zcsr_dpi_changed_fn dpi_cb;
    void* dpi_user;
    int min_w, min_h, max_w, max_h;
    zcsr_focus_fn focus_cb;
    void* focus_user;

#if defined(__ANDROID__)
    void* native_window;
#elif defined(_WIN32)
    void* hwnd;
    zcsr_window_mode mode;
    bool  fs_valid;
    long  fs_style;
    long  fs_exstyle;
    int   fs_x, fs_y, fs_w, fs_h;
    int   fs_display;
    bool  win_valid;
    int   win_x, win_y, win_w, win_h;
    bool  drag_region_enabled;
    int   drag_x, drag_y, drag_w, drag_h;
#elif defined(__linux__) && !defined(__ANDROID__)
    /* Wayland objects — stored as void* to keep this header free of
     * <wayland-client.h>; platform_wayland.c casts to concrete types. */
    void* wl_display;     /* struct wl_display*    */
    void* wl_registry;    /* struct wl_registry*   */
    void* wl_compositor;  /* struct wl_compositor* */
    void* xdg_wm_base;    /* struct xdg_wm_base*   */
    void* wl_seat;        /* struct wl_seat*       */
    void* wl_surface;     /* struct wl_surface*    */
    void* xdg_surface;    /* struct xdg_surface*   */
    void* xdg_toplevel;   /* struct xdg_toplevel*  */
    void* wl_keyboard;    /* struct wl_keyboard*   */
    void* wl_pointer;     /* struct wl_pointer*    */
    int   width, height;
    zcsr_window_mode mode;
    bool  configured;
    bool  close_requested;
#elif defined(__APPLE__)
    void* ns_window;
#else
    void* handle;
#endif
};

enum { ZCSR_SURFACE_POOL = 4 };

/* Fixed pool — defined in platform.c, shared by all backends. */
extern zcsr_surface zcsr_surfaces[ZCSR_SURFACE_POOL];

zcsr_surface* zcsr_surface_acquire(void);
void           zcsr_surface_release(zcsr_surface* s);
void           zcsr_surface_update_dpi_scale(zcsr_surface* s, float new_scale);
void           zcsr_surface_dispatch_focus(zcsr_surface* s, zcsr_focus_event event);

/* Display helpers shared by backends. */
void                 zcsr_display_set_name(zcsr_display_info* out, const char* name);
zcsr_display_info    zcsr_unknown_display(int id);

#endif /* ZCSR_PLATFORM_INTERNAL_H */
