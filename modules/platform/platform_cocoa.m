/* Cocoa (macOS) backend for the platform surface. Objective-C, compiled only on Apple.
 * Implements the C-ABI shim declared in platform_cocoa.h. Includes ONLY the same-module
 * shim/internal headers and the system Cocoa framework — no cross-module includes.
 *
 * 状态（如实记录）：
 *   - 6 个窗口/显示 API 已从"占位 false"改为真实 AppKit 实现（NSScreen / setStyleMask /
 *     setContentSize / contentRectForFrameRect / backingScaleFactor / toggleFullScreen）。
 *   - 仍未在真实 macOS 上编译验证（本机无 Apple toolchain）。首次在 Mac 上构建时，
 *     重点检查：ARC 下的 __bridge、maximumFramesPerSecond 的可用性、toggleFullScreen 异步。
 *   - 窗口默认样式仍是 borderless + 透明 + 置顶（overlay 语义，与 Win32 的 BORDERLESS 默认一致）。
 *     调用方必须显式 set_mode(WINDOWED) 才会得到正常应用窗口（标题栏/可缩放/普通层级）。
 */

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "platform_cocoa.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Ensure the process has an NSApplication so a window can appear from a plain C entry point. */
static void zcsr_cocoa_ensure_app(void) {
    static BOOL initialized = NO;
    if (initialized) return;
    initialized = YES;
    [NSApplication sharedApplication];
    /* Accessory: shows windows without a Dock icon / menu bar takeover — fits an overlay.
     * set_mode(WINDOWED) 会切到 Regular。 */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
}

void* zcsr_cocoa_window_create(const char* title, int x, int y, int w, int h) {
    @autoreleasepool {
        NSWindow* window;
        NSRect frame;
        zcsr_cocoa_ensure_app();

        frame = NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
        window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskBorderless
                        backing:NSBackingStoreBuffered
                          defer:NO];
        if (!window) return NULL;

        [window setTitle:[NSString stringWithUTF8String:(title ? title : "zcsr")]];
        [window setOpaque:NO];
        [window setBackgroundColor:[NSColor clearColor]];
        [window setLevel:NSStatusWindowLevel];   /* always-on-top overlay */
        [window setIgnoresMouseEvents:NO];
        /* overlay 无标题栏，用整窗拖动替代 Win32 的 drag region */
        [window setMovableByWindowBackground:YES];
        /* ARC 下由我们持有引用，close 时不要自动释放 */
        [window setReleasedWhenClosed:NO];
        [window makeKeyAndOrderFront:nil];

        /* Returned as an owning reference; balanced by zcsr_cocoa_window_destroy. */
        return (void*)CFBridgingRetain(window);
    }
}

void zcsr_cocoa_window_destroy(void* ns_window) {
    if (!ns_window) return;
    @autoreleasepool {
        NSWindow* window = (NSWindow*)CFBridgingRelease(ns_window);
        [window orderOut:nil];
        [window close];
    }
}

void zcsr_cocoa_window_pump(void* ns_window) {
    (void)ns_window;
    @autoreleasepool {
        NSEvent* event;
        /* Non-blocking drain of NON-key app events. Key down/up are intentionally LEFT in the
         * queue so zcsr_cocoa_input_pump (input module) consumes them — otherwise a typical loop
         * (surface pump before input pump) would swallow keys before input ever sees them. */
        const NSEventMask mask = NSEventMaskAny & ~(NSEventMaskKeyDown | NSEventMaskKeyUp);
        while ((event = [NSApp nextEventMatchingMask:mask
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil) {
            [NSApp sendEvent:event];
        }
    }
}

/* ---- 窗口模式 / 尺寸 / 缩放 ---- */

int zcsr_cocoa_window_set_mode(void* ns_window, int mode) {
    if (!ns_window || mode < 0 || mode > 2) return 0;
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        NSWindowStyleMask mask = [window styleMask];

        if (mode == 2) {   /* FULLSCREEN */
            if ((mask & NSWindowStyleMaskFullScreen) == 0) {
                [window toggleFullScreen:nil];
            }
            return 1;
        }
        if ((mask & NSWindowStyleMaskFullScreen) != 0) {
            [window toggleFullScreen:nil];  /* 先退出原生全屏 */
        }
        if (mode == 0) {   /* WINDOWED */
            [window setStyleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)];
            [window setLevel:NSNormalWindowLevel];
            [window setOpaque:YES];
            [window setBackgroundColor:[NSColor windowBackgroundColor]];
            [window setTitleVisibility:NSWindowTitleVisible];
            [window setMovableByWindowBackground:NO];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        } else {           /* BORDERLESS */
            [window setStyleMask:NSWindowStyleMaskBorderless];
            [window setLevel:NSStatusWindowLevel];
            [window setOpaque:NO];
            [window setBackgroundColor:[NSColor clearColor]];
            [window setMovableByWindowBackground:YES];
        }
        [window makeKeyAndOrderFront:nil];
    }
    return 1;
}

int zcsr_cocoa_window_set_content_size(void* ns_window, int w, int h) {
    if (!ns_window || w <= 0 || h <= 0) return 0;
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        if (([window styleMask] & NSWindowStyleMaskFullScreen) != 0) return 0;
        [window setContentSize:NSMakeSize((CGFloat)w, (CGFloat)h)];
    }
    return 1;
}

void zcsr_cocoa_window_get_content_size(void* ns_window, int* w, int* h) {
    if (!ns_window) return;
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        /* contentRectForFrameRect 扣掉标题栏/边框，得到真正的客户区 */
        NSRect content = [window contentRectForFrameRect:[window frame]];
        if (w) *w = (int)llround(content.size.width);
        if (h) *h = (int)llround(content.size.height);
    }
}

float zcsr_cocoa_window_backing_scale(void* ns_window) {
    CGFloat scale;
    if (!ns_window) return 1.0f;
    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        scale = [window backingScaleFactor];
    }
    return (scale > 0.0) ? (float)scale : 1.0f;
}

int zcsr_cocoa_window_move_to_screen(void* ns_window, int index) {
    if (!ns_window || index < 0) return 0;
    @autoreleasepool {
        NSArray<NSScreen*>* screens = [NSScreen screens];
        NSWindow* window = (__bridge NSWindow*)ns_window;
        NSRect frame, screen_frame;
        if (!screens || (NSUInteger)index >= [screens count]) return 0;
        screen_frame = [[screens objectAtIndex:(NSUInteger)index] frame];
        frame = [window frame];
        frame.origin.x = screen_frame.origin.x + (screen_frame.size.width - frame.size.width) / 2.0;
        frame.origin.y = screen_frame.origin.y + (screen_frame.size.height - frame.size.height) / 2.0;
        [window setFrame:frame display:YES];
    }
    return 1;
}

/* ---- 显示器枚举 ---- */

int zcsr_cocoa_screen_count(void) {
    @autoreleasepool {
        NSArray<NSScreen*>* screens = [NSScreen screens];
        return screens ? (int)[screens count] : 0;
    }
}

int zcsr_cocoa_screen_info(int index, int* out_id, char* out_name, int name_cap,
                           int* out_x, int* out_y, int* out_w, int* out_h,
                           int* out_refresh, float* out_scale, int* out_primary) {
    @autoreleasepool {
        NSArray<NSScreen*>* screens = [NSScreen screens];
        NSScreen* primary;
        NSScreen* screen;
        NSRect frame;
        CGFloat primary_top;

        if (!screens || index < 0 || (NSUInteger)index >= [screens count]) return 0;
        screen = [screens objectAtIndex:(NSUInteger)index];
        primary = [screens objectAtIndex:0];
        frame = [screen frame];
        primary_top = [primary frame].size.height;

        if (out_id) *out_id = index;
        /* AppKit 原点在左下，工程约定是左上：y_top = primaryHeight - (y + h) */
        if (out_x) *out_x = (int)llround(frame.origin.x);
        if (out_y) *out_y = (int)llround(primary_top - (frame.origin.y + frame.size.height));
        if (out_w) *out_w = (int)llround(frame.size.width);
        if (out_h) *out_h = (int)llround(frame.size.height);
        if (out_refresh) {
            int hz = (int)[screen maximumFramesPerSecond];
            *out_refresh = (hz > 0) ? hz : 0;
        }
        if (out_scale) {
            CGFloat s = [screen backingScaleFactor];
            *out_scale = (s > 0.0) ? (float)s : 1.0f;
        }
        if (out_primary) *out_primary = (index == 0) ? 1 : 0;
        if (out_name && name_cap > 0) {
            NSString* localized = [screen localizedName];
            const char* utf8 = localized ? [localized UTF8String] : NULL;
            snprintf(out_name, (size_t)name_cap, "%s", utf8 ? utf8 : "Display");
        }
    }
    return 1;
}

#endif /* __APPLE__ */

/* ---- C 侧：surface 生命周期 + 平台契约 ---- */
#if defined(__APPLE__)
#include "platform_cocoa.h"
#include "platform_internal.h"
#include <math.h>
#include <string.h>

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
    surface->mode = ZCSR_WINDOW_BORDERLESS;   /* 与 Win32 create 的默认一致 */
    surface->width = bounds.w;
    surface->height = bounds.h;

    surface->dpi_scale = zcsr_cocoa_window_backing_scale(window);
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

int zcsr_display_count(void) {
    int n = zcsr_cocoa_screen_count();
    return (n > 0) ? n : 1;   /* 契约：永远至少 1 */
}

bool zcsr_display_get(int index, zcsr_display_info* out) {
    int id = 0, x = 0, y = 0, w = 0, h = 0, refresh = 0, primary = 0;
    float scale = 1.0f;
    char name[64];

    if (!out) return false;
    memset(name, 0, sizeof(name));

    if (!zcsr_cocoa_screen_info(index, &id, name, (int)sizeof(name),
                                &x, &y, &w, &h, &refresh, &scale, &primary)) {
        /* 枚举失败时只允许 index 0 退回 fallback，保持 count>=1 的契约 */
        if (index != 0) return false;
        *out = zcsr_unknown_display(0);
        return true;
    }

    memset(out, 0, sizeof(*out));
    out->id = id;
    out->x = x; out->y = y;
    out->width = w; out->height = h;
    out->refresh_hz = refresh;
    out->dpi_scale = (scale > 0.0f) ? scale : 1.0f;
    out->is_primary = primary;
    zcsr_display_set_name(out, (name[0] != '\0') ? name : "Display");
    return true;
}

bool zcsr_surface_set_mode(zcsr_surface* s, zcsr_window_mode mode) {
    if (!s || !s->in_use || !s->ns_window) return false;
    if ((int)mode < (int)ZCSR_WINDOW_WINDOWED || (int)mode > (int)ZCSR_WINDOW_FULLSCREEN) return false;
    if (!zcsr_cocoa_window_set_mode(s->ns_window, (int)mode)) return false;
    s->mode = mode;
    if (mode == ZCSR_WINDOW_FULLSCREEN) {
        /* 全屏尺寸由 AppKit 决定，读回来作为新的客户区尺寸 */
        zcsr_cocoa_window_get_content_size(s->ns_window, &s->width, &s->height);
    }
    return true;
}

bool zcsr_surface_set_client_size(zcsr_surface* s, int w, int h) {
    if (!s || !s->in_use || !s->ns_window || w <= 0 || h <= 0) return false;
    if (s->mode == ZCSR_WINDOW_FULLSCREEN) return false;
    if (!zcsr_cocoa_window_set_content_size(s->ns_window, w, h)) return false;
    s->width = w;
    s->height = h;
    return true;
}

bool zcsr_surface_set_drag_region(zcsr_surface* s, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    if (!s || !s->in_use) return false;
    /* macOS 没有 Win32 的 HTCAPTION 区域：overlay 窗口在 create 时已设
     * movableByWindowBackground=YES，整窗可拖；这里如实返回成功。 */
    return true;
}

bool zcsr_surface_move_to_display(zcsr_surface* s, int display_id) {
    if (!s || !s->in_use || !s->ns_window) return false;
    if (display_id < 0) return false;
    return zcsr_cocoa_window_move_to_screen(s->ns_window, display_id) != 0;
}

void zcsr_surface_get_client_size(const zcsr_surface* s, int* w, int* h) {
    int cw = 0, ch = 0;
    if (!s || !s->in_use) return;
    zcsr_cocoa_window_get_content_size(s->ns_window, &cw, &ch);
    if (cw <= 0) cw = s->width;
    if (ch <= 0) ch = s->height;
    if (w) *w = cw;
    if (h) *h = ch;
}

float zcsr_surface_get_dpi_scale(const zcsr_surface* s) {
    float scale;
    if (!s || !s->in_use || !s->ns_window) return 1.0f;
    scale = zcsr_cocoa_window_backing_scale(s->ns_window);
    return (scale > 0.0f) ? scale : 1.0f;
}

#endif /* __APPLE__ */