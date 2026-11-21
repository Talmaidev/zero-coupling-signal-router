/* platform_android.c — Android platform backend. */
#if defined(__ANDROID__)
#include "platform_internal.h"
#include <math.h>
#include <android/configuration.h>
#include <android/native_window.h>
#include <dlfcn.h>

static int android_display_refresh_hz;

void zcsr_platform_android_set_display_refresh_hz(int hz) {
    android_display_refresh_hz = (hz > 0) ? hz : 0;
}

void zcsr_platform_android_set_config(AConfiguration* config) {
    float scale = 1.0f;
    int density;
    size_t i;
    if (!config) return;
    density = AConfiguration_getDensity(config);
    if (density > 0 && density < ACONFIGURATION_DENSITY_ANY) {
        scale = (float)density / 160.0f;
    }
    if (!isfinite(scale) || scale <= 0.0f) scale = 1.0f;
    for (i = 0; i < ZCSR_SURFACE_POOL; ++i) {
        if (zcsr_surfaces[i].in_use) {
            zcsr_surface_update_dpi_scale(&zcsr_surfaces[i], scale);
        }
    }
}

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

zcsr_surface* zcsr_surface_create_opaque(const char* title, zcsr_rect bounds) {
    /* The ANativeWindow is supplied by the app; opacity is a buffer/format
     * property, not a separate surface kind. Reuse the logical handle path. */
    return zcsr_surface_create(title, bounds);
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
 * the API cannot modify window mode/size/position. display_get has no surface
 * parameter, so it reports the active fullscreen ANativeWindow (the app is
 * fullscreen); if no window exists yet, the dimension remains the documented
 * unknown fallback rather than a fabricated "real" desktop size. */
int zcsr_display_count(void) {
    return 1;
}

bool zcsr_display_get(int index, zcsr_display_info* out) {
    size_t i;
    if (!out || index != 0) return false;
    zcsr_display_info info = zcsr_unknown_display(0);
    for (i = 0; i < ZCSR_SURFACE_POOL; ++i) {
        zcsr_surface* s = &zcsr_surfaces[i];
        ANativeWindow* window;
        if (!s->in_use || !s->native_window) continue;
        window = (ANativeWindow*)s->native_window;
        info.width = ANativeWindow_getWidth(window);
        info.height = ANativeWindow_getHeight(window);
        info.dpi_scale = zcsr_surface_get_dpi_scale(s);
        info.refresh_hz = android_display_refresh_hz;
        break;
    }
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
    if (!s) return 1.0f;
    return s->dpi_scale > 0.0f ? s->dpi_scale : 1.0f;
}


/* Android has no mouse cursor in most configurations — return false. */
bool zcsr_surface_set_cursor_shape(zcsr_surface* s, zcsr_cursor_shape shape) {
    (void)s; (void)shape; return false;
}
bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) { (void)s; (void)visible; return false; }
bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s, const unsigned char* rgba, int w, int h,
                                    int hot_x, int hot_y) { (void)s; (void)rgba; (void)w; (void)h; (void)hot_x; (void)hot_y; return false; }
bool zcsr_surface_reset_cursor(zcsr_surface* s) { (void)s; return false; }

/* Android 13+ has ANativeWindow_setFrameRate (API 30+) in libandroid.so.
 * The project still builds its shared library with min API 24, so resolve the
 * entry point dynamically: on API < 30 it is an honest false, on the target
 * Android 13+ device the platform really receives the preferred frame rate. */
typedef int32_t (*zcsr_set_frame_rate_fn)(ANativeWindow*, float, int8_t);
bool zcsr_surface_set_refresh_hz(zcsr_surface* s, int hz) {
    static zcsr_set_frame_rate_fn fn;
    static bool resolved;
    if (!s || !s->in_use || !s->native_window || hz <= 0) return false;
    if (!resolved) {
        void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (lib) fn = (zcsr_set_frame_rate_fn)dlsym(lib, "ANativeWindow_setFrameRate");
        resolved = true;
    }
    if (!fn) return false;
    return fn((ANativeWindow*)s->native_window, (float)hz,
              ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT) == 0;
}
#endif /* __ANDROID__ */

/* Keep the translation unit non-empty on platforms where the Android backend
 * is compiled out; MSVC C4206 is an error under /WX. */
typedef struct zcsr_platform_android_tu_anchor { int reserved; } zcsr_platform_android_tu_anchor;