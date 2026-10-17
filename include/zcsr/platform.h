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

/* ------------------------------------------------------------------ */
/* Display enumeration and window management (additive).              */
/* ------------------------------------------------------------------ */

/* One physical output/monitor. Coordinates are desktop-layout pixels;
 * width/height are the output pixel dimensions (not the window client size).
 * Unknown fields use documented fallbacks: refresh_hz=0, dpi_scale=1.0f. */
typedef struct {
    int   id;
    char  name[64];
    int   x, y;
    int   width, height;
    int   refresh_hz;
    float dpi_scale;
    int   is_primary;
} zcsr_display_info;

typedef enum {
    ZCSR_WINDOW_WINDOWED   = 0, /* normal decorated window */
    ZCSR_WINDOW_BORDERLESS = 1, /* undecorated, no fullscreen state */
    ZCSR_WINDOW_FULLSCREEN = 2, /* borderless window filling a display; never
                                   changes the physical output mode */
} zcsr_window_mode;

/* Number of currently connected displays. Always returns at least 1; when the
 * platform cannot enumerate displays it reports a single unknown fallback. */
int  zcsr_display_count(void);

/* Read display `index` (0-based in enumeration order). Returns false and does
 * not touch `out` when out==NULL or index is out of range. All fields are
 * initialized on success; name is NUL-terminated. */
bool zcsr_display_get(int index, zcsr_display_info* out);

/* Change the window decoration/fullscreen mode. FULLSCREEN fills the current
 * display without changing physical output resolution. Unsupported platforms
 * (Android) return false. Invalid mode/surface returns false. */
bool zcsr_surface_set_mode(zcsr_surface* surface, zcsr_window_mode mode);

/* Set the window CLIENT area size in pixels (excludes title bar/borders).
 * Returns false for non-positive sizes, invalid surface, or while fullscreen. */
bool zcsr_surface_set_client_size(zcsr_surface* surface, int w, int h);

/* Move the window onto the display whose stable id is `display_id`. Returns
 * false before touching the window for an invalid/disconnected id. While
 * fullscreen, the window stays fullscreen on the target display and the
 * original restore snapshot is preserved. */
bool zcsr_surface_move_to_display(zcsr_surface* surface, int display_id);

/* Read the current CLIENT area size in pixels. Either output pointer may be
 * NULL. Invalid surface leaves outputs untouched. */
void zcsr_surface_get_client_size(const zcsr_surface* surface, int* w, int* h);

/* Current DPI scale for the window's display; 1.0f when unknown. */
float zcsr_surface_get_dpi_scale(const zcsr_surface* surface);

typedef void (*zcsr_dpi_changed_fn)(
    zcsr_surface* surface,
    float new_scale,
    void* user
);

/*
 * Registers the per-surface DPI callback.
 * A later registration replaces the previous callback.
 * Passing NULL unregisters the callback.
 * Registration does not invoke the callback immediately.
 * The callback runs synchronously during platform event dispatch.
 * Do NOT destroy the surface or modify the same window state inside
 * the callback; doing so may cause reentrancy issues.
 */
void zcsr_surface_set_dpi_callback(
    zcsr_surface* surface,
    zcsr_dpi_changed_fn callback,
    void* user
);

/*
 * Set minimum and maximum client-area sizes for the window.
 * A value of 0 means "no limit" on that axis.
 * Returns false for negative values or when min > max on the same axis.
 * On Windows the limits are hard-enforced (WM_GETMINMAXINFO).
 * On Linux they are WM hints (XSizeHints); the window manager may ignore them.
 * On Android/macOS this is unsupported and returns false.
 * Limits may be set while fullscreen; they take effect after leaving fullscreen.
 */
bool zcsr_surface_set_size_limits(
    zcsr_surface* surface,
    int min_w, int min_h,
    int max_w, int max_h
);

#endif /* ZCSR_PLATFORM_H */
