/* Agent 5 — Cocoa (macOS) backend for the platform surface. Objective-C, compiled only on Apple.
 * Implements the C-ABI shim declared in platform_cocoa.h. Includes ONLY the same-module shim
 * header and the system Cocoa framework — no cross-module includes (zero-coupling preserved).
 *
 * NOTE: written on a Linux host without an Apple toolchain — NOT compiled/run here. Needs a Mac
 * (clang + Cocoa framework) to build and verify. */
#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "platform_cocoa.h"

/* Ensure the process has an NSApplication so a window can appear from a plain C entry point. */
static void zcsr_cocoa_ensure_app(void) {
    static BOOL initialized = NO;
    if (initialized) return;
    initialized = YES;
    [NSApplication sharedApplication];
    /* Accessory: shows windows without a Dock icon / menu bar takeover — fits an overlay. */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
}

void* zcsr_cocoa_window_create(const char* title, int x, int y, int w, int h) {
    @autoreleasepool {
        zcsr_cocoa_ensure_app();

        NSRect frame = NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskBorderless
                        backing:NSBackingStoreBuffered
                          defer:NO];
        if (!window) return NULL;

        [window setTitle:[NSString stringWithUTF8String:(title ? title : "zcsr")]];
        [window setOpaque:NO];
        [window setBackgroundColor:[NSColor clearColor]];
        [window setLevel:NSStatusWindowLevel];   /* always-on-top */
        [window setIgnoresMouseEvents:NO];
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

#endif /* __APPLE__ */

/* macOS platform backend (appended to existing cocoa overlay code). */
#if defined(__APPLE__)
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

#endif /* __APPLE__ */
