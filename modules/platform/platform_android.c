/* platform_android.c — Android platform backend. */
#if defined(__ANDROID__)
#include "platform_internal.h"
#include <android/native_window.h>

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

bool zcsr_surface_attach_native(zcsr_surface* s, void* native_window) {
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
}

void zcsr_surface_detach_native(zcsr_surface* s) {
    ANativeWindow* previous;
    if (!s || !s->in_use) return;
    previous = (ANativeWindow*)s->native_window;
    s->native_window = 0;
    if (previous) {
        ANativeWindow_release(previous);
    }
}

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


/* Android has no mouse cursor in most configurations — return false. */
bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) { (void)s; (void)visible; return false; }
bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s, const unsigned char* rgba, int w, int h,
                                    int hot_x, int hot_y) { (void)s; (void)rgba; (void)w; (void)h; (void)hot_x; (void)hot_y; return false; }
bool zcsr_surface_reset_cursor(zcsr_surface* s) { (void)s; return false; }
#endif /* __ANDROID__ */
