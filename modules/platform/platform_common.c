/* platform_common.c — shared pool, DPI, focus, display helpers, unknown-platform stubs. */
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

void zcsr_surface_dispatch_focus(
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

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) && !defined(__linux__)
zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds) { (void)title; (void)bounds; return 0; }
void          zcsr_surface_destroy(zcsr_surface* s)                    { zcsr_surface_release(s); }
void*         zcsr_surface_native_handle(const zcsr_surface* s)        { (void)s; return 0; }
void*         zcsr_surface_native_display(const zcsr_surface* s)       { (void)s; return 0; }
void          zcsr_surface_pump_events(zcsr_surface* s)                { (void)s; }
bool          zcsr_surface_attach_native(zcsr_surface* s, void* nw)   { (void)s; (void)nw; return false; }
void          zcsr_surface_detach_native(zcsr_surface* s)              { (void)s; }
#endif

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
void zcsr_display_set_name(zcsr_display_info* out, const char* name) {
    zcsr_copy_name(out->name, sizeof(out->name), name);
}

/* A conservative "unknown single display" fallback. Width/height/refresh are
 * 0 (unknown), dpi is 1.0; never fabricate a resolution or refresh rate. */
zcsr_display_info zcsr_unknown_display(int id) {
    zcsr_display_info info;
    memset(&info, 0, sizeof(info));
    info.id = id;
    info.dpi_scale = 1.0f;
    info.is_primary = 1;
    zcsr_display_set_name(&info, "Unknown");
    return info;
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) && !defined(__linux__)
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

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) && !defined(__linux__)

/* Unknown stub — return false (no cursor support). */
bool zcsr_surface_set_cursor_visible(zcsr_surface* s, bool visible) { (void)s; (void)visible; return false; }
bool zcsr_surface_set_cursor_bitmap(zcsr_surface* s, const unsigned char* rgba, int w, int h,
                                    int hot_x, int hot_y) { (void)s; (void)rgba; (void)w; (void)h; (void)hot_x; (void)hot_y; return false; }
bool zcsr_surface_reset_cursor(zcsr_surface* s) { (void)s; return false; }

#endif
