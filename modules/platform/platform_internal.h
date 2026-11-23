#ifndef ZCSR_PLATFORM_INTERNAL_H
#define ZCSR_PLATFORM_INTERNAL_H

/* X11 key event ring capacity (platform_x11.c uses the same literal). */
enum { ZCSR_X11_KEY_RING = 64 };
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
    /* WM_CLOSE 只置位，由应用决定是否真退出（与 X11/Wayland 的 close_requested 对齐）。
     * 以前这里直接 ShowWindow(SW_HIDE)，应用永远看不到关闭请求，未保存确认被绕过。 */
    bool  close_requested;
#elif defined(__linux__) && !defined(__ANDROID__)
    /* Wayland objects — stored as void* to keep this header free of
     * <wayland-client.h>; platform_wayland.c casts to concrete types. */
    void* wl_display;     /* struct wl_display*    */
    void* wl_registry;    /* struct wl_registry*   */
    void* wl_compositor;  /* struct wl_compositor* */
    void* xdg_wm_base;    /* struct xdg_wm_base*   */
    void* wl_seat;        /* struct wl_seat*       */
    void* wl_output;      /* struct wl_output*     */
    void* wl_surface;     /* struct wl_surface*    */
    void* xdg_surface;    /* struct xdg_surface*   */
    void* xdg_toplevel;   /* struct xdg_toplevel*  */
    int   width, height;
    int   output_width, output_height;
    int   output_refresh;
    int   output_scale;
    char  output_name[64];
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
    unsigned long x_cursor_shape;         /* 当前标准形状光标，切换时释放 */
    int           x_mouse_x, x_mouse_y;   /* 客户区坐标 */
    unsigned int  x_mouse_buttons;        /* 按键掩码   */
    unsigned int  x_buttons_latch;        /* 两次 pump 之间按下的边沿（读一次后清） */
    unsigned int  x_mouse_mods;           /* 最近一次按键/移动事件的 X modifier mask */
    int           x_down_x, x_down_y;     /* 左键按下时的 client 坐标 */
    unsigned int  x_down_mods;            /* 左键按下时的 X modifier mask */
    bool          x_have_down;            /* 左键当前按住；抬起或失焦时清 */
    int           x_wheel;                /* 累计滚轮刻度 */
    /* XIM/XIC：X11 的 UTF-8 文本生产者。平台泵负责事件过滤和查找，
     * inputext 通过 zcsr_platform_x11_take_text 取走字节。 */
    void*         x_im;                  /* XIM */
    void*         x_ic;                  /* XIC */
    char          x_text_buf[256];
    size_t        x_text_head, x_text_tail;
    /* 按键事件环：平台泵入，input 模块泵出。 */
    struct { unsigned int keycode; int down; } x_keys[64];
    int x_key_head, x_key_tail;
#elif defined(__APPLE__)
    void* ns_window;
    void* ns_delegate;   /* retained ZCSRWindowDelegate*; released by surface destroy */
    /* NSWindowDelegate 只置位；应用决定是否退出。程序化销毁必须先置
     * delegate.forceClose，避免被自己的 windowShouldClose: veto 卡住。 */
    bool  close_requested;
    /* 窗口状态：create 默认 BORDERLESS（与 Win32 一致），set_mode 才会切换。
     * width/height 是客户区 client pixel（不是 AppKit point）；Cocoa 在
     * create/set_client_size/get_client_size 边界按 backingScaleFactor 换算，
     * 与 GL viewport 和输入坐标保持同一空间。get_dpi_scale 返回 backingScaleFactor。 */
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
void                 zcsr_copy_name(char* dst, size_t cap, const char* src);
void                 zcsr_display_set_name(zcsr_display_info* out, const char* name);
zcsr_display_info    zcsr_unknown_display(int id);

#endif /* ZCSR_PLATFORM_INTERNAL_H */
