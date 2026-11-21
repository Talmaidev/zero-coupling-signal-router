/* glrender_platform_headless.c — no-window backend (ZCSR_LINUX_WINDOW_BACKEND=none).
 *
 * Headless builds still link zcsr_glrender (CPU tests, UI runtime tests, tools), but
 * there is no GL context to create. platform_init therefore fails deliberately, so
 * zcsr_gl_create() returns NULL before any GL call is made; the remaining entry
 * points are no-ops. This keeps the headless target linkable without pretending to
 * render anything.
 */
#include "glrender_internal.h"

#if defined(__linux__) && !defined(__ANDROID__) \
    && !defined(ZCSR_LINUX_BACKEND_X11) && !defined(ZCSR_LINUX_BACKEND_WAYLAND)

bool zcsr_gl_platform_init(zcsr_gl_renderer* r, zcsr_surface* surface) {
    (void)surface;
    if (r) {
        r->width = 1;
        r->height = 1;
    }
    return false; /* no GL context in a headless build */
}

void zcsr_gl_platform_destroy(zcsr_gl_renderer* r) { (void)r; }

bool zcsr_gl_make_current(zcsr_gl_renderer* r) { (void)r; return false; }

void zcsr_gl_update_size(zcsr_gl_renderer* r) { (void)r; }

void zcsr_gl_swap(zcsr_gl_renderer* r) { (void)r; }

void zcsr_gl_platform_set_vsync(zcsr_gl_renderer* r, bool on) {
    (void)r;
    (void)on;
}

#endif /* Linux && !Android && no window backend */

#if !(defined(__linux__) && !defined(__ANDROID__) \
      && !defined(ZCSR_LINUX_BACKEND_X11) && !defined(ZCSR_LINUX_BACKEND_WAYLAND))
typedef int zcsr_gl_headless_unused_tu_;
#endif
