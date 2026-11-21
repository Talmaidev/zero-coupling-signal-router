/* platform_caps.c — truthful compile-time capability report.
 *
 * This is a query, not a switch. Intel/AMD iGPU support and window/input
 * operation never depend on an application reading or forwarding these bits.
 * A feature is set only when the compiled backend implements it today.
 */
#include "zcsr/platform.h"

unsigned zcsr_platform_capabilities(void) {
#if defined(_WIN32)
    return ZCSR_PLATFORM_CAP_DISPLAY |
           ZCSR_PLATFORM_CAP_INPUT |
           ZCSR_PLATFORM_CAP_FOCUS_EVENTS |
           ZCSR_PLATFORM_CAP_CONTENT_SIZE |
           ZCSR_PLATFORM_CAP_CURSOR_SHAPE |
           ZCSR_PLATFORM_CAP_CURSOR_BITMAP |
           ZCSR_PLATFORM_CAP_IME_COMMIT;
#elif defined(__APPLE__)
    return ZCSR_PLATFORM_CAP_DISPLAY |
           ZCSR_PLATFORM_CAP_INPUT |
           ZCSR_PLATFORM_CAP_FOCUS_EVENTS |
           ZCSR_PLATFORM_CAP_CONTENT_SIZE |
           ZCSR_PLATFORM_CAP_CURSOR_SHAPE |
           ZCSR_PLATFORM_CAP_IME_COMMIT;
#elif defined(__ANDROID__)
    return ZCSR_PLATFORM_CAP_DISPLAY |
           ZCSR_PLATFORM_CAP_INPUT |
           ZCSR_PLATFORM_CAP_FOCUS_EVENTS |
           ZCSR_PLATFORM_CAP_CONTENT_SIZE |
           ZCSR_PLATFORM_CAP_ASSETS;
#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_X11)
    return ZCSR_PLATFORM_CAP_DISPLAY |
           ZCSR_PLATFORM_CAP_INPUT |
           ZCSR_PLATFORM_CAP_FOCUS_EVENTS |
           ZCSR_PLATFORM_CAP_CONTENT_SIZE |
           ZCSR_PLATFORM_CAP_CURSOR_SHAPE |
           ZCSR_PLATFORM_CAP_IME_COMMIT;
#elif defined(__linux__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)
    return ZCSR_PLATFORM_CAP_DISPLAY |
           ZCSR_PLATFORM_CAP_INPUT |
           ZCSR_PLATFORM_CAP_FOCUS_EVENTS |
           ZCSR_PLATFORM_CAP_CONTENT_SIZE;
#else
    return 0u;
#endif
}
