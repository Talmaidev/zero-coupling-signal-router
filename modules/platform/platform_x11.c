/* platform_x11.c — Linux / X11 窗口后端。
 *
 * 编译门槛：CMake 选 X11 后端时定义 ZCSR_LINUX_BACKEND_X11；Wayland 构建里
 * 本文件整文件被剔除。同理 platform_wayland.c 也按它自己的宏剔除，两者不会
 * 在同一平台上重复定义 zcsr_surface_* 符号。
 *
 * 设计要点：
 *   - 零堆：surface 来自 platform.c 的固定池，事件存在结构体里的定长环，不 malloc。
 *   - 事件所有权：平台泵负责 XNextEvent，把结果按类型分流到按键环 / 鼠标状态 /
 *     尺寸 / 关闭标志；input 与 inputext 通过 extern 访问器取（与 Wayland 的
 *     zcsr_platform_wayland_* 同一约定，不新增跨模块头）。
 *   - 键码：X11 keycode 与引擎 zcsr_keycode 同域（引擎 Linux 侧约定 keycode = evdev + 8，
 *     正是 X11 的 keycode 定义），因此直接透传，不再做 XKB 映射。
 *   - 全屏走 _NET_WM_STATE_FULLSCREEN（EWMH），无边框走 _MOTIF_WM_HINTS，
 *     不改物理分辨率 —— 与引擎契约"window mode 不改变输出模式"一致。
 */

#if defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11)

#include "platform_internal.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define X_DISPLAY(s) ((Display*)(s)->x_display)
#define X_WIN(s)     ((Window)(s)->x_window)

/* _MOTIF_WM_HINTS：无边框窗口的通用做法（GNOME/KDE/XFCE 都识别）。 */
typedef struct {
    unsigned long flags, functions, decorations;
    long input_mode;
    unsigned long status;
} motif_wm_hints;

#define MOTIF_HINTS_DECORATIONS  (1L << 1)
#define MWM_HINTS_FUNCTIONS      (1L << 0)
#ifndef MWM_FUNC_ALL
#define MWM_FUNC_ALL   (1L << 0)
#endif
#ifndef MWM_DECOR_ALL
#define MWM_DECOR_ALL  (1L << 0)
#endif

static int x_error_handler(Display* d, XErrorEvent* e) {
    (void)d;
    /* 默认 handler 直接 abort；这里降级成日志，窗口操作失败不该终止进程。 */
    fprintf(stderr, "[x11] X error: request=%d code=%d\n",
            (int)e->request_code, (int)e->error_code);
    return 0;
}

/* ---------------- 按键环 ---------------- */
static void x_push_key(zcsr_surface* s, unsigned int keycode, int down) {
    int next = (s->x_key_head + 1) % (int)ZCSR_X11_KEY_RING;
    if (next == s->x_key_tail) return;         /* 环满：丢最旧，避免覆盖 */
    s->x_keys[s->x_key_head].keycode = keycode;
    s->x_keys[s->x_key_head].down = down;
    s->x_key_head = next;
}

bool zcsr_platform_x11_next_key(zcsr_surface* s, unsigned int* keycode, int* is_down) {
    if (!s || s->x_key_tail == s->x_key_head) return false;
    if (keycode) *keycode = s->x_keys[s->x_key_tail].keycode;
    if (is_down) *is_down = s->x_keys[s->x_key_tail].down;
    s->x_key_tail = (s->x_key_tail + 1) % (int)ZCSR_X11_KEY_RING;
    return true;
}

/* ---------------- 鼠标状态（inputext 消费） ---------------- */
/* buttons 返回"物理按住"与"上一帧以来按下过的边沿"的并集：这样一次快速
 * 按下+抬起（两次 pump 之间完成）也能被上层看到一次，不会整帧丢失。 */
bool zcsr_platform_x11_mouse_state(zcsr_surface* s, int* x, int* y,
                                   unsigned int* buttons, int* wheel) {
    if (!s) return false;
    if (x) *x = s->x_mouse_x;
    if (y) *y = s->x_mouse_y;
    if (buttons) *buttons = s->x_mouse_buttons | s->x_buttons_latch;
    if (wheel) *wheel = s->x_wheel;
    s->x_buttons_latch = 0;
    return true;
}

/* 滚轮读取后清零：与 Win32 的 wheel_accum 语义一致，一次泵只累加一次。 */
int zcsr_platform_x11_take_wheel(zcsr_surface* s) {
    int w;
    if (!s) return 0;
    w = s->x_wheel;
    s->x_wheel = 0;
    return w;
}

bool zcsr_platform_x11_take_close(zcsr_surface* s) {
    bool r;
    if (!s) return false;
    r = s->close_requested;
    s->close_requested = false;
    return r;
}

bool zcsr_platform_x11_is_focused(const zcsr_surface* s) {
    return s ? s->x_focused : false;
}

void* zcsr_platform_x11_display(const zcsr_surface* s) {
    return s ? s->x_display : NULL;
}

unsigned long zcsr_platform_x11_window(const zcsr_surface* s) {
    return s ? s->x_window : 0ul;
}

/* ---------------- 窗口模式 ---------------- */
static void x_set_fullscreen(zcsr_surface* s, bool on) {
    XEvent ev;
    Atom state = XInternAtom(X_DISPLAY(s), "_NET_WM_STATE", False);
    Atom full  = XInternAtom(X_DISPLAY(s), "_NET_WM_STATE_FULLSCREEN", False);
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = X_WIN(s);
    ev.xclient.message_type = state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = on ? 1 : 0;          /* _NET_WM_STATE_ADD / REMOVE */
    ev.xclient.data.l[1] = (long)full;
    ev.xclient.data.l[2] = 0;
    XSendEvent(X_DISPLAY(s), DefaultRootWindow(X_DISPLAY(s)), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(X_DISPLAY(s));
}

static void x_set_borderless(zcsr_surface* s, bool on) {
    motif_wm_hints hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = MOTIF_HINTS_DECORATIONS | MWM_HINTS_FUNCTIONS;
    hints.functions = on ? 0ul : (unsigned long)(MWM_FUNC_ALL);
    hints.decorations = on ? 0ul : (unsigned long)(MWM_DECOR_ALL);
    long data[5] = { (long)hints.flags, (long)hints.functions,
                     (long)hints.decorations, 0L, (long)hints.status };
    Atom prop = XInternAtom(X_DISPLAY(s), "_MOTIF_WM_HINTS", False);
    XChangeProperty(X_DISPLAY(s), X_WIN(s), prop, prop, 32, PropModeReplace,
                    (unsigned char*)data, 5);
    XFlush(X_DISPLAY(s));
}

/* ---------------- create / destroy ---------------- */
zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) {
    zcsr_surface* s;
    Display* dpy;
    Window win;
    XSetWindowAttributes swa;
    XSizeHints* hints;
    long mask;

    s = zcsr_surface_acquire();
    if (!s) return NULL;

    s->width  = bounds.w > 0 ? bounds.w : 800;
    s->height = bounds.h > 0 ? bounds.h : 600;
    s->mode = ZCSR_WINDOW_WINDOWED;
    s->close_requested = false;
    s->x_key_head = s->x_key_tail = 0;
    s->x_wheel = 0;
    s->x_mouse_buttons = 0;

    dpy = XOpenDisplay(NULL);
    if (!dpy) return NULL;          /* 没有 DISPLAY，调用方会看到 NULL 而不是崩溃 */
    s->x_display = dpy;
    s->x_screen = DefaultScreen(dpy);

    XSetErrorHandler(x_error_handler);

    swa.background_pixmap = None;
    swa.border_pixel = 0;
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | ExposureMask |
                     FocusChangeMask | EnterWindowMask | LeaveWindowMask;
    mask = CWBackPixmap | CWBorderPixel | CWEventMask;

    win = XCreateWindow(dpy, RootWindow(dpy, s->x_screen),
                        bounds.x, bounds.y,
                        (unsigned)s->width, (unsigned)s->height,
                        0, CopyFromParent, InputOutput, CopyFromParent,
                        mask, &swa);
    if (!win) { XCloseDisplay(dpy); s->x_display = NULL; return NULL; }

    s->x_window = (unsigned long)win;
    s->x_atom_wm_delete = (long)XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, (Atom*)&s->x_atom_wm_delete, 1);

    if (title) XStoreName(dpy, win, title);

    hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PPosition | PSize;
        hints->x = bounds.x;
        hints->y = bounds.y;
        hints->width = s->width;
        hints->height = s->height;
        XSetWMNormalHints(dpy, win, hints);
        XFree(hints);
    }

    XMapWindow(dpy, win);
    XFlush(dpy);
    s->x_mapped = true;
    return s;
}

void zcsr_surface_destroy(zcsr_surface* s) {
    if (!s) return;
    if (s->x_display) {
        Display* dpy = X_DISPLAY(s);
        if (s->x_window) {
            XDestroyWindow(dpy, X_WIN(s));
            s->x_window = 0;
        }
        XCloseDisplay(dpy);
        s->x_display = NULL;
    }
    zcsr_surface_release(s);
}

/* ---------------- event pump ---------------- */
void zcsr_surface_pump_events(zcsr_surface* s) {
    Display* dpy;
    if (!s || !s->x_display) return;
    dpy = X_DISPLAY(s);

    while (XPending(dpy) > 0) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case KeyPress:
            x_push_key(s, (unsigned int)ev.xkey.keycode, 1);
            break;
        case KeyRelease:
            /* X11 auto-repeat 会成对发 Release/Press；当前按原始事件上报，
             * 上层以 key_down 表为准，重复不影响边沿判定。 */
            x_push_key(s, (unsigned int)ev.xkey.keycode, 0);
            break;
        case MotionNotify:
            s->x_mouse_x = ev.xmotion.x;
            s->x_mouse_y = ev.xmotion.y;
            break;
        case ButtonPress:
            if (ev.xbutton.button == Button4)      s->x_wheel += 1;
            else if (ev.xbutton.button == Button5) s->x_wheel -= 1;
            else {
                s->x_mouse_buttons |= (1u << (ev.xbutton.button & 31u));
                s->x_buttons_latch |= (1u << (ev.xbutton.button & 31u));
                s->x_mouse_x = ev.xbutton.x;
                s->x_mouse_y = ev.xbutton.y;
            }
            break;
        case ButtonRelease:
            if (ev.xbutton.button != Button4 && ev.xbutton.button != Button5) {
                s->x_mouse_buttons &= ~(1u << (ev.xbutton.button & 31u));
                s->x_mouse_x = ev.xbutton.x;
                s->x_mouse_y = ev.xbutton.y;
            }
            break;
        case ConfigureNotify:
            if (ev.xconfigure.width > 0 && ev.xconfigure.height > 0) {
                s->width = ev.xconfigure.width;
                s->height = ev.xconfigure.height;
            }
            break;
        case FocusIn:
            s->x_focused = true;
            zcsr_surface_dispatch_focus(s, ZCSR_FOCUS_GAINED);
            break;
        case FocusOut:
            s->x_focused = false;
            memset(s->x_keys, 0, sizeof(s->x_keys));
            s->x_key_head = s->x_key_tail = 0;
            s->x_mouse_buttons = 0;
            zcsr_surface_dispatch_focus(s, ZCSR_FOCUS_LOST);
            break;
        case ClientMessage:
            if ((long)ev.xclient.data.l[0] == s->x_atom_wm_delete) {
                s->close_requested = true;
            }
            break;
        default:
            break;
        }
    }
    XFlush(dpy);
}

/* ---------------- native handles ---------------- */
void* zcsr_surface_native_handle(const zcsr_surface* s) {
    return s ? (void*)(uintptr_t)s->x_window : NULL;   /* X11 Window */
}
void* zcsr_surface_native_display(const zcsr_surface* s) {
    return s ? s->x_display : NULL;                   /* Display*  */
}

/* ---------------- size ---------------- */
void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    if (!s) return;
    if (w) *w = s->width;
    if (h) *h = s->height;
}

bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    if (!s || w <= 0 || h <= 0 || !s->x_display) return false;
    /* 契约：fullscreen 期间不允许改 client size（与 Win32/Wayland 一致）。 */
    if (s->mode == ZCSR_WINDOW_FULLSCREEN) return false;
    XResizeWindow(X_DISPLAY(s), X_WIN(s), (unsigned)w, (unsigned)h);
    XFlush(X_DISPLAY(s));
    s->width = w;
    s->height = h;
    return true;
}

float zcsr_surface_get_dpi_scale(const zcsr_surface* s) { (void)s; return 1.0f; }

/* ---------------- display enumeration ---------------- */
int zcsr_display_count(void) { return 1; }

bool zcsr_display_get(int index, zcsr_display_info* out) {
    Display* dpy;
    if (!out || index != 0) return false;
    memset(out, 0, sizeof(*out));
    dpy = XOpenDisplay(NULL);
    if (dpy) {
        int scr = DefaultScreen(dpy);
        out->width  = DisplayWidth(dpy, scr);
        out->height = DisplayHeight(dpy, scr);
        out->x = 0; out->y = 0;
        XCloseDisplay(dpy);
    } else {
        out->width = 1920;
        out->height = 1080;
    }
    out->id = 0;
    out->refresh_hz = 0;
    out->dpi_scale = 1.0f;
    out->is_primary = 1;
    zcsr_display_set_name(out, "X11");
    return true;
}

/* ---------------- window mode ---------------- */
bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    if (!s || !s->x_display) return false;
    if (mode != ZCSR_WINDOW_WINDOWED && mode != ZCSR_WINDOW_BORDERLESS &&
        mode != ZCSR_WINDOW_FULLSCREEN) return false;

    if (mode == ZCSR_WINDOW_FULLSCREEN) {
        x_set_borderless(s, false);
        x_set_fullscreen(s, true);
    } else if (mode == ZCSR_WINDOW_BORDERLESS) {
        x_set_fullscreen(s, false);
        x_set_borderless(s, true);
    } else {
        x_set_fullscreen(s, false);
        x_set_borderless(s, false);
    }
    s->mode = mode;
    return true;
}

zcsr_window_mode zcsr_surface_get_mode(const zcsr_surface* s) {
    return s ? s->mode : ZCSR_WINDOW_WINDOWED;
}

/* ---------------- 光标 ---------------- */
bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) {
    Display* dpy;
    if (!s || !s->x_display) return false;
    dpy = X_DISPLAY(s);

    if (visible) {
        XUndefineCursor(dpy, X_WIN(s));
    } else {
        /* 1×1 全透明 pixmap 作光标：X11 隐藏指针的标准做法，不依赖 XRender。 */
        if (!s->x_cursor_blank) {
            Pixmap px = XCreatePixmap(dpy, X_WIN(s), 1, 1, 1);
            XColor dummy;
            memset(&dummy, 0, sizeof(dummy));
            s->x_cursor_blank = (unsigned long)XCreatePixmapCursor(
                dpy, px, px, &dummy, &dummy, 0, 0);
            XFreePixmap(dpy, px);
        }
        if (s->x_cursor_blank) {
            XDefineCursor(dpy, X_WIN(s), (Cursor)s->x_cursor_blank);
        }
    }
    XFlush(dpy);
    return true;
}

bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s, const unsigned char* rgba,
                                    int w, int h, int hot_x, int hot_y) {
    (void)s; (void)rgba; (void)w; (void)h; (void)hot_x; (void)hot_y;
    return false;   /* 自定义 RGBA 光标（需要 XRender）留待后续 */
}

bool zcsr_surface_reset_cursor(zcsr_surface* s) {
    return zcsr_surface_set_cursor_visible(s, true);
}

/* ---------------- 其余契约（X11 v1 最小实现） ---------------- */
void zcsr_surface_apply_size_limits(zcsr_surface* s) { (void)s; }

bool zcsr_surface_set_drag_region(zcsr_surface* s, int x, int y, int w, int h) {
    (void)s; (void)x; (void)y; (void)w; (void)h;
    return false;
}

bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    (void)s; (void)display_id;
    return false;
}

/* Android 契约在桌面平台不支持。 */
bool zcsr_surface_attach_native(zcsr_surface* s, void* native_window) {
    (void)s; (void)native_window; return false;
}
void zcsr_surface_detach_native(zcsr_surface* s) { (void)s; }

#endif /* defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11) */

#if !(defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_X11))
typedef int zcsr_platform_x11_unused_tu_;
#endif