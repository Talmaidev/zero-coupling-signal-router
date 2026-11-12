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

    /* X11 objects — same void* / 整数约定；platform_x11.c 负责转型。 */
    void*         x_display;   /* Display*     */
    unsigned long x_window;    /* Window (XID) */
    int           x_screen;
    long          x_atom_wm_delete;
    bool          x_mapped;
    bool          x_focused;
    unsigned long x_cursor_blank;         /* 隐藏指针用的 1×1 光标 */
    int           x_mouse_x, x_mouse_y;   /* 客户区坐标 */
    unsigned int  x_mouse_buttons;        /* 按键掩码   */
    unsigned int  x_buttons_latch;        /* 两次 pump 之间按下的边沿（读一次后清） */
    int           x_wheel;                /* 累计滚轮刻度 */
    /* 按键事件环：平台泵入，input 模块泵出。 */
    enum { ZCSR_X11_KEY_RING = 64 };
    struct { unsigned int keycode; int down; } x_keys[ZCSR_X11_KEY_RING];
    int x_key_head, x_key_tail;
#elif defined(__APPLE__)
    void* ns_window;
    /* 窗口状态：create 默认 BORDERLESS（与 Win32 一致），set_mode 才会切换。
     * width/height 是客户区尺寸（point，不是 backing pixel）；get_client_size 以它为准，
     * get_dpi_scale 返回 backingScaleFactor（Retina 为 2.0）。 */
    zcsr_window_mode mode;
    int   width, height;
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
