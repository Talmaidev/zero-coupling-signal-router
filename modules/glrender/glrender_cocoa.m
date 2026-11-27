/* macOS / NSOpenGL backend for the GL 3.3 renderer.
 *
 * Compiled ONLY on APPLE (the zcsr_module() CMake function globs *.m there and enables -fobjc-arc).
 * It defines nothing but the platform hooks declared in glrender_internal.h; every platform-
 * independent GL routine (program, VAO/VBO, texture pool, batching, text) stays in glrender.c.
 *
 * The NSOpenGLContext is an Objective-C object that must survive inside the plain-C renderer
 * struct. Under ARC it is handed into the void* slot with __bridge_retained (retained) and handed
 * back with __bridge_transfer on destroy (released). No -release/-retain calls (ARC forbids them).
 */
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>

#include "glrender_internal.h"

static NSOpenGLContext* zcsr_gl_ctx(zcsr_gl_renderer* r) {
    return (r && r->ctx) ? (__bridge NSOpenGLContext*)r->ctx : nil;
}

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    NSWindow* win = (__bridge NSWindow*)zcsr_surface_native_handle(surface);
    if (!win) return false;
    NSView* view = [win contentView];
    if (!view) return false;

    NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
        NSOpenGLPFAColorSize, 32,
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFAAccelerated,
        0
    };
    NSOpenGLPixelFormat* pix = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    if (!pix) return false;
    NSOpenGLContext* ctx = [[NSOpenGLContext alloc] initWithFormat:pix shareContext:nil];
    if (!ctx) return false;
    [ctx setView:view];
    [ctx makeCurrentContext];

    if (!gladLoadGLLoader((GLADloadproc)NSOpenGLGetProcAddress)) {
        [ctx setView:nil];
        return false;
    }

    r->ctx = (__bridge_retained void*)ctx;
    zcsr_gl_update_size(r);
    return true;
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) {
    if (!r || !r->ctx) return;
    NSOpenGLContext* ctx = (__bridge_transfer NSOpenGLContext*)r->ctx; /* ARC releases at scope end */
    r->ctx = NULL;
    [NSOpenGLContext clearCurrentContext];
    [ctx setView:nil];
    (void)ctx;
}

bool zcsr_gl_make_current(zcsr_gl_renderer* r) {
    NSOpenGLContext* ctx = zcsr_gl_ctx(r);
    if (!ctx) return false;
    /* makeCurrentContext is a round-trip; skip it when already current. */
    if ([NSOpenGLContext currentContext] == ctx) return true;
    [ctx makeCurrentContext];
    return true;
}

void zcsr_gl_update_size(zcsr_gl_renderer* r) {
    NSWindow* win;
    NSView* view;
    NSRect bounds;
    NSRect backing;
    int old_w;
    int old_h;
    if (!r || !r->surface) return;
    win = (__bridge NSWindow*)zcsr_surface_native_handle(r->surface);
    view = [win contentView];
    if (!view) return;
    bounds = [view bounds];
    /* OpenGL drawables are in backing pixels, not points. On a Retina display
     * using bounds directly would render at half resolution and make
     * glReadPixels disagree with the viewport. */
    backing = [view convertRectToBacking:bounds];
    old_w = r->width;
    old_h = r->height;
    r->width = backing.size.width > 0 ? (int)backing.size.width : 1;
    r->height = backing.size.height > 0 ? (int)backing.size.height : 1;
    if (r->width != old_w || r->height != old_h) {
        NSOpenGLContext* ctx = zcsr_gl_ctx(r);
        if (ctx) [ctx update];
    }
}

void zcsr_gl_swap(zcsr_gl_renderer* r) {
    [zcsr_gl_ctx(r) flushBuffer];
}

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    NSOpenGLContext* ctx = zcsr_gl_ctx(r);
    GLint interval;
    if (!ctx) return;
    interval = on ? 1 : 0;
    [ctx setValues:&interval forParameter:NSOpenGLCPSwapInterval];
}
