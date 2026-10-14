#ifndef ZCSR_PLATFORM_H
#define ZCSR_PLATFORM_H
#include <stdbool.h>
/* Agent 5 — Platform abstraction. Win32 / X11 / Cocoa / Android, auto-selected at compile time.
 * NO OpenGL/DirectX/Vulkan. Exactly one backend is selected by the gates below.
 * Implementer: modules/platform. */

typedef enum {
    ZCSR_PLATFORM_UNKNOWN = 0,
    ZCSR_PLATFORM_WINDOWS,
    ZCSR_PLATFORM_LINUX,
    ZCSR_PLATFORM_MACOS,
    ZCSR_PLATFORM_ANDROID
} zcsr_platform;

#if defined(_WIN32)
#  define ZCSR_PLATFORM ZCSR_PLATFORM_WINDOWS
#elif defined(__APPLE__)
#  define ZCSR_PLATFORM ZCSR_PLATFORM_MACOS
#elif defined(__ANDROID__)
#  define ZCSR_PLATFORM ZCSR_PLATFORM_ANDROID
#elif defined(__linux__)
#  define ZCSR_PLATFORM ZCSR_PLATFORM_LINUX
#else
#  define ZCSR_PLATFORM ZCSR_PLATFORM_UNKNOWN
#endif

typedef struct { int x, y, w, h; } zcsr_rect;

typedef struct zcsr_surface zcsr_surface; /* opaque native top-level surface */

/* Desktop: create the native surface. Android: create a logical handle and attach the
 * system-provided ANativeWindow separately. Returns NULL on failure. */
zcsr_surface* zcsr_surface_create(const char* title, zcsr_rect bounds);
void          zcsr_surface_destroy(zcsr_surface*);
void*         zcsr_surface_native_handle(const zcsr_surface*); /* HWND / X11 Window / NSWindow* / ANativeWindow* */
/* Backend-owned drawing connection, so renderers (overlay) REUSE it instead of opening their
 * own per frame. X11: the `Display*`. Win32/Cocoa: NULL (the native handle is sufficient). */
void*         zcsr_surface_native_display(const zcsr_surface*);
void          zcsr_surface_pump_events(zcsr_surface*);
/* Android lifecycle binding.
 * The caller retains its own native-window reference. On success, the platform
 * module acquires an independent reference. Non-Android backends do not support it.
 */
bool zcsr_surface_attach_native(zcsr_surface* surface, void* native_window);
void zcsr_surface_detach_native(zcsr_surface* surface);

#endif /* ZCSR_PLATFORM_H */
