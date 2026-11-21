#ifndef ZCSR_INPUTEXT_INTERNAL_H
#define ZCSR_INPUTEXT_INTERNAL_H
/* Module-private shared definitions for the inputext module.
 * Included only by translation units under modules/inputext/.
 * Same-directory internal header is allowed by the zero-coupling guard. */

#include "zcsr/input_ext.h"
#include "zcsr/platform.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct zcsr_input_ext {
    zcsr_mouse_state mouse;
    uint8_t mouse_prev_buttons;  /* held mask from the previous pump, for edge derivation */
    zcsr_pad_state pads[ZCSR_PAD_MAX];
    zcsr_surface* surface;
    bool initialized;
    /* First valid pointer sample: prevents a huge dx/dy from the zero-initialized
     * position on every backend (Win32, X11, Wayland, Cocoa). */
    bool mouse_have_pos;

    /* Cross-backend event accumulators. Producers append; zcsr_input_ext_pump
     * publishes and clears. This is what keeps an event-driven backend
     * (Wayland/Cocoa) equivalent to a polled backend (Win32/X11). */
    int wheel_accum;
    int wheel_remainder;
    uint8_t mouse_btn_pressed;
    uint8_t mouse_btn_released;
    int mouse_dx_accum;
    int mouse_dy_accum;

    /* UTF-8 text queue. Every platform with a text producer writes here;
     * zcsr_input_ext_take_text drains complete UTF-8 sequences only. */
    char text_buf[256];
    size_t text_count;
    uint16_t text_high_surrogate;

#if defined(__ANDROID__)
    zcsr_touch_state touch;
    zcsr_touch_state touch_pending;
    int32_t touch_pointer_id;
    int32_t pad_device_id[ZCSR_PAD_MAX];
    /* JNI bridge to android.view.KeyEvent.getUnicodeChar. Native AKeyEvent
     * has no Unicode conversion API, so the only real physical-keyboard text
     * source is the framework KeyEvent, using the device's active keymap. */
    void* java_vm;
    void* key_event_class;
    void* key_event_ctor;
    void* key_event_unicode;
#elif defined(_WIN32)
    bool mouse_inside;
    void* mouse_subclass_hwnd;  /* HWND */
#elif defined(__linux__)
    /* Wayland objects are owned here, not by platform_wayland.c: the seat can
     * outlive a surface, and destroying the seat before child objects is a
     * protocol error. X11 uses platform-owned state. */
#  if defined(ZCSR_LINUX_BACKEND_WAYLAND)
    void* wl_pointer;   /* struct wl_pointer*  */
    void* wl_keyboard;  /* struct wl_keyboard* */
    void* wl_seat;      /* struct wl_seat*     */
    void* xkb_context;  /* struct xkb_context* */
    void* xkb_keymap;   /* struct xkb_keymap*  */
    void* xkb_state;    /* struct xkb_state*   */
    int   wheel_axis_accum_fixed; /* wl_fixed_t accumulator for axis-only compositors */
    bool  wheel_discrete_seen;    /* axis_discrete already described this frame */
#  endif
    int gamepad_fd[ZCSR_PAD_MAX];
    char gamepad_path[ZCSR_PAD_MAX][32];
    int gamepad_abs_min[ZCSR_PAD_MAX][6];
    int gamepad_abs_max[ZCSR_PAD_MAX][6];
    int gamepad_scan_tick;
    bool gamepad_init;
#elif defined(__APPLE__)
    void* event_monitor;  /* id<NSObject> local NSEvent monitor */
    void* ns_window;      /* NSWindow* target */
#endif
};

/* Internal entry points — implemented per-backend in inputext.c /
 * inputext_wayland.c / inputext_linux_gamepad.c / inputext_cocoa.m. */
void zcsr_scan_gamepads(zcsr_input_ext* in);
void zcsr_close_gamepads(zcsr_input_ext* in);
void zcsr_pump_gamepads(zcsr_input_ext* in);
void zcsr_select_mouse_events(zcsr_input_ext* in, zcsr_surface* window);
void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window);

/* Common mouse metadata producers. `mods` must be ZCSR_MOD_* bits. */
void zcsr_mouse_record_left_down(zcsr_input_ext* in, int x, int y, uint8_t mods);
void zcsr_mouse_clear_down(zcsr_input_ext* in);

/* Shared producers. Kept external so the Cocoa/Wayland translation units can
 * feed the same queue without duplicating UTF-8 conversion. */
void zcsr_input_text_push_utf8(zcsr_input_ext* in, const char* bytes, size_t n);

#endif /* ZCSR_INPUTEXT_INTERNAL_H */
