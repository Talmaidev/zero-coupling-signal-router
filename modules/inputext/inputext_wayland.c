/* inputext_wayland.c — real Wayland pointer + physical-keyboard text backend.
 *
 * The platform module owns the seat/surface. This file owns the child
 * wl_pointer/wl_keyboard objects so they are destroyed before the seat, and
 * uses xkbcommon to turn the compositor keymap into UTF-8. Soft-keyboard/IME
 * requires zwp_text_input_v3 and is not silently claimed here.
 *
 * Gamepad lives in inputext_linux_gamepad.c (evdev, shared with X11).
 * Zero zcsr heap; xkbcommon allocates internally.
 */
#if defined(__linux__) && !defined(__ANDROID__) && defined(ZCSR_LINUX_BACKEND_WAYLAND)

#include "inputext_internal.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern void* zcsr_platform_wayland_seat(const zcsr_surface* s);

#ifndef BTN_LEFT
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112
#endif

/* ---- wl_pointer ---- */
static uint8_t zcsr_wayland_mods(void* state_ptr) {
    struct xkb_state* state = (struct xkb_state*)state_ptr;
    uint8_t mods = 0;
    if (!state) return 0;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= ZCSR_MOD_CTRL;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= ZCSR_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= ZCSR_MOD_ALT;
    if (xkb_state_mod_name_is_active(state, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE) > 0) mods |= ZCSR_MOD_GUI;
    return mods;
}

static void ptr_enter(void* data, struct wl_pointer* ptr, uint32_t serial,
                      struct wl_surface* surf, wl_fixed_t sx, wl_fixed_t sy) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)serial; (void)surf;
    if (!in) return;
    in->mouse.x = wl_fixed_to_int(sx);
    in->mouse.y = wl_fixed_to_int(sy);
    in->mouse_dx_accum = 0;
    in->mouse_dy_accum = 0;
    in->mouse_have_pos = true;
}

static void ptr_leave(void* data, struct wl_pointer* ptr, uint32_t serial,
                      struct wl_surface* surf) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)serial; (void)surf;
    if (!in) return;
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
    if (!in->mouse_have_pos) {
        in->mouse_have_pos = true;
        in->mouse_dx_accum = 0;
        in->mouse_dy_accum = 0;
    } else {
        in->mouse_dx_accum += nx - in->mouse.x;
        in->mouse_dy_accum += ny - in->mouse.y;
    }
    in->mouse.x = nx;
    in->mouse.y = ny;
}

static void ptr_button(void* data, struct wl_pointer* ptr, uint32_t serial,
                       uint32_t time, uint32_t button, uint32_t state) {
    zcsr_input_ext* in = data;
    uint8_t bit = 0;
    (void)ptr; (void)serial; (void)time;
    if (!in) return;
    if (button == BTN_LEFT) bit = ZCSR_MOUSE_LEFT;
    else if (button == BTN_RIGHT) bit = ZCSR_MOUSE_RIGHT;
    else if (button == BTN_MIDDLE) bit = ZCSR_MOUSE_MIDDLE;
    else return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        in->mouse.buttons = (uint8_t)(in->mouse.buttons | bit);
        in->mouse_btn_pressed = (uint8_t)(in->mouse_btn_pressed | bit);
        if (button == BTN_LEFT) {
            zcsr_mouse_record_left_down(in, in->mouse.x, in->mouse.y,
                                        zcsr_wayland_mods(in->xkb_state));
        }
    } else {
        in->mouse.buttons = (uint8_t)(in->mouse.buttons & (uint8_t)~bit);
        in->mouse_btn_released = (uint8_t)(in->mouse_btn_released | bit);
        if (button == BTN_LEFT) zcsr_mouse_clear_down(in);
    }
}

static void ptr_axis(void* data, struct wl_pointer* ptr, uint32_t time,
                     uint32_t axis, wl_fixed_t value) {
    zcsr_input_ext* in = data;
    (void)ptr; (void)time;
    if (!in || axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    /* Wayland positive axis = scroll down; zcsr/Win32 positive = scroll up.
     * Keep the fixed-point value and resolve it in frame(), after any
     * axis_discrete event for the same scroll step has arrived. */
    in->wheel_axis_accum_fixed += value;
}

static void ptr_frame(void* data, struct wl_pointer* ptr) {
    zcsr_input_ext* in = data;
    const int fixed_per_step = 10 * 256; /* wl_fixed_t: 256 per unit */
    int steps;
    (void)ptr;
    if (!in) return;
    if (!in->wheel_discrete_seen) {
        /* Compositors without axis_discrete report the standard 10-unit
         * smooth-scroll notch. Fractional remainders carry to the next frame. */
        steps = in->wheel_axis_accum_fixed / fixed_per_step;
        if (steps != 0) {
            in->wheel_accum -= steps;
            in->wheel_axis_accum_fixed -= steps * fixed_per_step;
        }
    } else {
        in->wheel_discrete_seen = false;
    }
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
    if (!in || axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    /* The protocol already gives the step count. Do not also convert the
     * companion axis event, or one wheel notch becomes two ticks. */
    in->wheel_accum -= discrete;
    in->wheel_axis_accum_fixed = 0;
    in->wheel_discrete_seen = true;
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

/* ---- wl_keyboard -> xkbcommon -> UTF-8 ---- */
static void keyboard_keymap(void* data, struct wl_keyboard* kb, uint32_t format,
                            int32_t fd, uint32_t size) {
    zcsr_input_ext* in = data;
    void* map;
    struct xkb_context* ctx;
    struct xkb_keymap* keymap;
    struct xkb_state* state;

    (void)kb;
    if (!in) { close(fd); return; }
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0u) {
        close(fd);
        return;
    }

    ctx = (struct xkb_context*)in->xkb_context;
    if (!ctx) {
        ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        in->xkb_context = ctx;
    }
    if (!ctx) { close(fd); return; }

    map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;

    /* zero-copy: xkbcommon parses the mapped buffer directly, so this backend
     * keeps the no-heap contract even while replacing the keymap live. */
    keymap = xkb_keymap_new_from_buffer(ctx, (const char*)map, (size_t)size,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) return;

    state = xkb_state_new(keymap);
    if (!state) { xkb_keymap_unref(keymap); return; }

    if (in->xkb_state) xkb_state_unref((struct xkb_state*)in->xkb_state);
    if (in->xkb_keymap) xkb_keymap_unref((struct xkb_keymap*)in->xkb_keymap);
    in->xkb_keymap = keymap;
    in->xkb_state = state;
}

static void keyboard_enter(void* data, struct wl_keyboard* kb, uint32_t serial,
                           struct wl_surface* surf, struct wl_array* keys) {
    zcsr_input_ext* in = data;
    (void)kb; (void)serial; (void)surf; (void)keys;
    if (in && in->surface)
        zcsr_surface_notify_focus(in->surface, true);
}

static void keyboard_leave(void* data, struct wl_keyboard* kb, uint32_t serial,
                           struct wl_surface* surf) {
    zcsr_input_ext* in = data;
    (void)kb; (void)serial; (void)surf;
    if (!in) return;
    in->mouse.buttons = 0;
    zcsr_mouse_clear_down(in);
    if (in->surface)
        zcsr_surface_notify_focus(in->surface, false);
}

static void keyboard_key(void* data, struct wl_keyboard* kb, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    zcsr_input_ext* in = data;
    struct xkb_state* xkb_state;
    int is_ctrl;
    int is_alt;
    char utf8[64];
    int n;
    (void)kb; (void)serial; (void)time;
    if (!in || !in->xkb_state) return;
    xkb_state = (struct xkb_state*)in->xkb_state;

    /* Linux evdev key code + 8 == XKB keycode. */
    if (xkb_state_update_key(xkb_state, key + 8u,
            state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP) < 0) {
        return;
    }
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (!xkb_state_key_get_utf8(xkb_state, key + 8u, utf8, sizeof utf8)) return;

    is_ctrl = xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL,
                                          XKB_STATE_MODS_EFFECTIVE) > 0;
    is_alt = xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_ALT,
                                         XKB_STATE_MODS_EFFECTIVE) > 0;
    if (is_ctrl || is_alt) return;

    n = (int)strlen(utf8);
    if (n > 0) zcsr_input_text_push_utf8(in, utf8, (size_t)n);
}

static void keyboard_modifiers(void* data, struct wl_keyboard* kb, uint32_t serial,
                               uint32_t dep, uint32_t lat, uint32_t lock,
                               uint32_t group) {
    zcsr_input_ext* in = data;
    struct xkb_state* xkb_state;
    (void)kb; (void)serial;
    if (!in || !in->xkb_state) return;
    xkb_state = (struct xkb_state*)in->xkb_state;
    xkb_state_update_mask(xkb_state, dep, lat, lock, 0, 0, group);
}

static void keyboard_repeat_info(void* data, struct wl_keyboard* kb,
                                 int32_t rate, int32_t delay) {
    (void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener kb_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

/* ---- lifecycle ---- */
static void zcsr_wayland_input_teardown(zcsr_input_ext* in) {
    if (!in) return;
    if (in->wl_keyboard) {
        wl_keyboard_destroy((struct wl_keyboard*)in->wl_keyboard);
        in->wl_keyboard = NULL;
    }
    if (in->wl_pointer) {
        wl_pointer_destroy((struct wl_pointer*)in->wl_pointer);
        in->wl_pointer = NULL;
    }
    if (in->xkb_state) {
        xkb_state_unref((struct xkb_state*)in->xkb_state);
        in->xkb_state = NULL;
    }
    if (in->xkb_keymap) {
        xkb_keymap_unref((struct xkb_keymap*)in->xkb_keymap);
        in->xkb_keymap = NULL;
    }
    if (in->xkb_context) {
        xkb_context_unref((struct xkb_context*)in->xkb_context);
        in->xkb_context = NULL;
    }
    in->wl_seat = NULL;
}

void zcsr_select_mouse_events(zcsr_input_ext* in, zcsr_surface* window) {
    struct wl_seat* seat;
    if (!in) return;
    if (!window) {
        zcsr_wayland_input_teardown(in);
        return;
    }
    if (in->wl_seat && in->surface == window) return;
    zcsr_wayland_input_teardown(in);

    seat = (struct wl_seat*)zcsr_platform_wayland_seat(window);
    if (!seat) return;
    in->wl_seat = seat;
    in->wl_pointer = wl_seat_get_pointer(seat);
    if (in->wl_pointer) {
        wl_pointer_add_listener((struct wl_pointer*)in->wl_pointer,
                                &ptr_listener, in);
    }
    in->wl_keyboard = wl_seat_get_keyboard(seat);
    if (in->wl_keyboard) {
        wl_keyboard_add_listener((struct wl_keyboard*)in->wl_keyboard,
                                 &kb_listener, in);
    }
}

void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    if (!in) return;
    if (!window || !in->wl_pointer) {
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
        in->mouse_dx_accum = 0;
        in->mouse_dy_accum = 0;
        in->wheel_accum = 0;
        in->wheel_remainder = 0;
        in->wheel_axis_accum_fixed = 0;
        in->wheel_discrete_seen = false;
        in->mouse_have_pos = false;
        zcsr_mouse_clear_down(in);
        return;
    }
    in->mouse.dx = in->mouse_dx_accum;
    in->mouse.dy = in->mouse_dy_accum;
    in->mouse.wheel = in->wheel_accum;
    in->mouse_dx_accum = 0;
    in->mouse_dy_accum = 0;
    in->wheel_accum = 0;
}

#endif /* Wayland */

typedef struct zcsr_inputext_wayland_tu_anchor { int reserved; } zcsr_inputext_wayland_tu_anchor;
