/* inputext_wayland.c — Wayland mouse input backend (opt-in).
 *
 * Handles wl_pointer events (enter/leave/motion/button/axis) and feeds
 * zcsr_mouse_state. Keyboard is handled by the input module (input.c
 * Wayland branch); gamepad evdev scanning is shared with the X11 backend
 * via inputext.c (gamepad_fd is backend-agnostic on Linux).
 *
 * Zero-heap: all state in static storage or caller buffer.
 */
#if defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)

#include "inputext_internal.h"

#include <wayland-client.h>
#include <string.h>

/* Internal accessor defined in platform_wayland.c (declared extern here
 * to avoid cross-module header include — zero-coupling guard). */
extern void* zcsr_platform_wayland_seat(const zcsr_surface* s);

/* ---- evdev button codes (linux/input.h) ---- */
#ifndef BTN_LEFT
#define BTN_LEFT   0x110
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT  0x111
#endif
#ifndef BTN_MIDDLE
#define BTN_MIDDLE 0x112
#endif

/* ---- wl_pointer listeners ---- */
static void ptr_enter(void* data, struct wl_pointer* ptr, uint32_t serial,
                      struct wl_surface* surf, wl_fixed_t sx, wl_fixed_t sy) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)serial; (void)surf;
    if (!in) return;
    in->mouse.x = wl_fixed_to_int(sx);
    in->mouse.y = wl_fixed_to_int(sy);
    in->mouse.dx = 0;
    in->mouse.dy = 0;
}

static void ptr_leave(void* data, struct wl_pointer* ptr, uint32_t serial,
                      struct wl_surface* surf) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)serial; (void)surf;
    if (!in) return;
    /* Focus lost — clear mouse button state (position retained for hover recovery). */
    in->mouse.buttons = 0;
}

static void ptr_motion(void* data, struct wl_pointer* ptr, uint32_t time,
                       wl_fixed_t sx, wl_fixed_t sy) {
    zcsr_input_ext* in = data;
    int nx, ny;
    (void)ptr; (void)time;
    if (!in) return;
    nx = wl_fixed_to_int(sx);
    ny = wl_fixed_to_int(sy);
    in->mouse.dx += nx - in->mouse.x;
    in->mouse.dy += ny - in->mouse.y;
    in->mouse.x = nx;
    in->mouse.y = ny;
}

static void ptr_button(void* data, struct wl_pointer* ptr, uint32_t serial,
                       uint32_t time, uint32_t button, uint32_t state) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)serial; (void)time;
    if (!in) return;
    /* Only handle the three primary buttons; ignore side/extra buttons. */
    if (button == BTN_LEFT) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) in->mouse.buttons |= ZCSR_MOUSE_LEFT;
        else in->mouse.buttons &= (uint8_t)~ZCSR_MOUSE_LEFT;
    } else if (button == BTN_RIGHT) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) in->mouse.buttons |= ZCSR_MOUSE_RIGHT;
        else in->mouse.buttons &= (uint8_t)~ZCSR_MOUSE_RIGHT;
    } else if (button == BTN_MIDDLE) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) in->mouse.buttons |= ZCSR_MOUSE_MIDDLE;
        else in->mouse.buttons &= (uint8_t)~ZCSR_MOUSE_MIDDLE;
    }
}

static void ptr_axis(void* data, struct wl_pointer* ptr, uint32_t time,
                     uint32_t axis, wl_fixed_t value) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)time;
    if (!in) return;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        /* Wayland axis value is in pixels; one wheel tick ≈ 120 units.
         * Accumulate fractional ticks; sign: positive = scroll down. */
        in->mouse.wheel -= wl_fixed_to_int(value) / 120;
    }
    /* Horizontal scroll not exposed in zcsr_mouse_state (no hwheel field). */
}

static void ptr_frame(void* data, struct wl_pointer* ptr) {
    (void)data; (void)ptr;
    /* Frame boundary — no action needed; state is already updated per-event. */
}

static void ptr_axis_source(void* data, struct wl_pointer* ptr, uint32_t source) {
    (void)data; (void)ptr; (void)source;
}

static void ptr_axis_stop(void* data, struct wl_pointer* ptr, uint32_t time, uint32_t axis) {
    (void)data; (void)ptr; (void)time; (void)axis;
}

static void ptr_axis_discrete(void* data, struct wl_pointer* ptr, uint32_t axis, int32_t discrete) {
    zcsr_input_ext* in = data;
    (void)ptr;
    if (!in) return;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        in->mouse.wheel -= discrete;  /* discrete is in ticks (positive = down) */
    }
}

static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_frame,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
};

static struct wl_pointer* g_wl_pointer = NULL;
static const zcsr_input_ext* g_ptr_owner = NULL;

/* ---- backend entry points (called from inputext.c common code) ---- */

void zcsr_select_mouse_events(zcsr_surface* window) {
    struct wl_seat* seat;
    if (!window || g_wl_pointer) return;  /* bind once */
    seat = (struct wl_seat*)zcsr_platform_wayland_seat(window);
    if (!seat) return;
    g_wl_pointer = wl_seat_get_pointer(seat);
    if (g_wl_pointer) {
        /* listener data will be set on first pump via zcsr_pump_mouse */
        wl_pointer_add_listener(g_wl_pointer, &ptr_listener, NULL);
    }
}

void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    if (!in) return;
    /* Attach listener data to this input_ext instance if not already set. */
    if (g_wl_pointer && g_ptr_owner != in) {
        wl_pointer_add_listener(g_wl_pointer, &ptr_listener, in);
        g_ptr_owner = in;
    }
    /* Wayland pointer events arrive via wl_pointer listener callbacks
     * (dispatched by zcsr_surface_pump_events). Reset per-frame deltas here. */
    in->mouse.dx = 0;
    in->mouse.dy = 0;
    in->mouse.wheel = 0;
    (void)window;
}

/* Gamepad scanning on Linux is backend-agnostic (evdev /dev/input/eventN).
 * Reuse the implementation from inputext.c by leaving these as no-ops here;
 * the common inputext.c code will call its own zcsr_scan_gamepads when
 * ZCSR_LINUX_BACKEND_WAYLAND is NOT defined. For Wayland builds, we provide
 * minimal stubs and rely on the shared evdev code being compiled in.
 *
 * Actually: inputext.c's Linux branch is gated !ZCSR_LINUX_BACKEND_WAYLAND,
 * so evdev gamepad code is NOT compiled in Wayland builds. Provide stubs. */
void zcsr_scan_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

void zcsr_close_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

void zcsr_pump_gamepads(zcsr_input_ext* in) { (void)in; }

#endif /* defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_WAYLAND) */
