/* Engine-ext Agent 3 - universal input extension.
 * Additive mouse/gamepad state beside the existing keyboard module. Includes only contracts plus
 * native backend headers. State and queues live entirely in the caller buffer.
 */
#include "zcsr/input_ext.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <xinput.h>
#endif

struct zcsr_input_ext {
    zcsr_mouse_state mouse;
    zcsr_pad_state pads[ZCSR_PAD_MAX];
    zcsr_surface* surface;
    bool initialized;
#if defined(__ANDROID__)
    zcsr_touch_state touch;
    zcsr_touch_state touch_pending;
    int32_t touch_pointer_id;
    int32_t pad_device_id[ZCSR_PAD_MAX];
#elif defined(__linux__) && !defined(__ANDROID__)
    int gamepad_fd[ZCSR_PAD_MAX];
#elif defined(_WIN32)
    bool mouse_have_pos;
    bool mouse_inside;
    HWND mouse_subclass_hwnd;
    int wheel_accum;
    int wheel_remainder;
    uint8_t mouse_btn_pressed;   /* buttons pressed since last pump (event-driven) */
    uint8_t mouse_btn_released;  /* buttons released since last pump (event-driven) */
#endif
};

#if defined(__ANDROID__)
#include <android/input.h>
#include <math.h>

static void zcsr_scan_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    in->touch_pointer_id = -1;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pad_device_id[i] = -1;
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

static void zcsr_close_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pad_device_id[i] = -1;
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

static void zcsr_select_mouse_events(zcsr_surface* window) { (void)window; }

static void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    (void)window;
    if (!in) return;
    in->mouse.dx = 0;
    in->mouse.dy = 0;
    in->mouse.wheel = 0;
}

static void zcsr_pump_gamepads(zcsr_input_ext* in) { (void)in; }

static int zcsr_android_pad_slot(zcsr_input_ext* in, int32_t device_id) {
    int i, free_slot = -1;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        if (in->pad_device_id[i] == device_id) return i;
        if (free_slot < 0 && in->pad_device_id[i] < 0) free_slot = i;
    }
    if (free_slot < 0) return -1;
    in->pad_device_id[free_slot] = device_id;
    in->pads[free_slot].connected = true;
    return free_slot;
}

static void zcsr_android_set_button(zcsr_pad_state* pad, zcsr_pad_button button, int down) {
    uint16_t bit;
    if (!pad || button < 0 || button >= ZCSR_PAD_BUTTON_COUNT) return;
    bit = (uint16_t)(1u << (unsigned)button);
    if (down) pad->buttons = (uint16_t)(pad->buttons | bit);
    else pad->buttons = (uint16_t)(pad->buttons & (uint16_t)~bit);
}

static float zcsr_android_clamp_axis(float v, float lo, float hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

int32_t zcsr_input_ext_android_event(
    zcsr_input_ext* in, AInputEvent* event) {
    int32_t type;
    int32_t source;
    if (!in || !event) return 0;
    type = AInputEvent_getType(event);
    source = AInputEvent_getSource(event);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t device_id, keycode, action, slot;
        zcsr_pad_button button;
        if ((source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK |
                       AINPUT_SOURCE_DPAD)) == 0) return 0;
        device_id = AInputEvent_getDeviceId(event);
        slot = zcsr_android_pad_slot(in, device_id);
        if (slot < 0) return 0;
        keycode = AKeyEvent_getKeyCode(event);
        action = AKeyEvent_getAction(event);
        switch (keycode) {
        case AKEYCODE_BUTTON_A:      button = ZCSR_PAD_A; break;
        case AKEYCODE_BUTTON_B:      button = ZCSR_PAD_B; break;
        case AKEYCODE_BUTTON_X:      button = ZCSR_PAD_X; break;
        case AKEYCODE_BUTTON_Y:      button = ZCSR_PAD_Y; break;
        case AKEYCODE_BUTTON_L1:     button = ZCSR_PAD_LB; break;
        case AKEYCODE_BUTTON_R1:     button = ZCSR_PAD_RB; break;
        case AKEYCODE_BUTTON_SELECT: button = ZCSR_PAD_BACK; break;
        case AKEYCODE_BUTTON_START:  button = ZCSR_PAD_START; break;
        case AKEYCODE_BUTTON_THUMBL: button = ZCSR_PAD_LSTICK; break;
        case AKEYCODE_BUTTON_THUMBR: button = ZCSR_PAD_RSTICK; break;
        case AKEYCODE_DPAD_UP:       button = ZCSR_PAD_DUP; break;
        case AKEYCODE_DPAD_DOWN:     button = ZCSR_PAD_DDOWN; break;
        case AKEYCODE_DPAD_LEFT:     button = ZCSR_PAD_DLEFT; break;
        case AKEYCODE_DPAD_RIGHT:    button = ZCSR_PAD_DRIGHT; break;
        default: return 0;
        }
        zcsr_android_set_button(&in->pads[slot], button, action == AKEY_EVENT_ACTION_DOWN);
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((source & AINPUT_SOURCE_TOUCHSCREEN) != 0) {
            int32_t action_full = AMotionEvent_getAction(event);
            int32_t action = action_full & AMOTION_EVENT_ACTION_MASK;
            int32_t ptr_index = (action_full & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                in->touch_pointer_id = AMotionEvent_getPointerId(event, 0);
                in->touch_pending.x = AMotionEvent_getX(event, 0);
                in->touch_pending.y = AMotionEvent_getY(event, 0);
                in->touch_pending.active = true;
                in->touch_pending.pressed = true;
                break;
            case AMOTION_EVENT_ACTION_MOVE: {
                size_t count = AMotionEvent_getPointerCount(event);
                size_t i;
                for (i = 0; i < count; ++i) {
                    if (AMotionEvent_getPointerId(event, (int32_t)i) == in->touch_pointer_id) {
                        float nx = AMotionEvent_getX(event, (int32_t)i);
                        float ny = AMotionEvent_getY(event, (int32_t)i);
                        in->touch_pending.dx += nx - in->touch_pending.x;
                        in->touch_pending.dy += ny - in->touch_pending.y;
                        in->touch_pending.x = nx;
                        in->touch_pending.y = ny;
                        break;
                    }
                }
                break;
            }
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                if (AMotionEvent_getPointerId(event, ptr_index) == in->touch_pointer_id) {
                    in->touch_pending.released = true;
                    in->touch_pending.active = false;
                    in->touch_pointer_id = -1;
                }
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                in->touch_pending.released = true;
                in->touch_pending.active = false;
                in->touch_pointer_id = -1;
                break;
            default:
                return 0;
            }
            return 1;
        }
        if ((source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK)) != 0) {
            int32_t device_id = AInputEvent_getDeviceId(event);
            int slot = zcsr_android_pad_slot(in, device_id);
            zcsr_pad_state* pad;
            float lx, ly, rx, ry, lt, rt;
            if (slot < 0) return 0;
            pad = &in->pads[slot];
            lx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
            ly = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            rx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
            ry = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
            lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
            rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
            /* Trigger fallback: some devices report triggers on Z/RZ. */
            if (lt <= 0.0f && rx > 0.0f) { lt = rx; rx = 0.0f; }
            if (rt <= 0.0f && ry > 0.0f) { rt = ry; ry = 0.0f; }
            if (fabsf(lx) < 0.15f) lx = 0.0f;
            if (fabsf(ly) < 0.15f) ly = 0.0f;
            if (fabsf(rx) < 0.15f) rx = 0.0f;
            if (fabsf(ry) < 0.15f) ry = 0.0f;
            pad->lx = zcsr_android_clamp_axis(lx, -1.0f, 1.0f);
            pad->ly = zcsr_android_clamp_axis(ly, -1.0f, 1.0f);
            pad->rx = zcsr_android_clamp_axis(rx, -1.0f, 1.0f);
            pad->ry = zcsr_android_clamp_axis(ry, -1.0f, 1.0f);
            pad->lt = zcsr_android_clamp_axis(lt, 0.0f, 1.0f);
            pad->rt = zcsr_android_clamp_axis(rt, 0.0f, 1.0f);
            return 1;
        }
        return 0;
    }
    return 0;
}

void zcsr_input_ext_android_reset(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    in->touch_pending.active = false;
    in->touch_pending.pressed = false;
    in->touch_pending.released = true;
    in->touch_pointer_id = -1;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        in->pads[i].buttons = 0;
        in->pads[i].lx = in->pads[i].ly = in->pads[i].rx = in->pads[i].ry = 0.0f;
        in->pads[i].lt = in->pads[i].rt = 0.0f;
    }
}

#elif defined(__linux__) && !defined(__ANDROID__)
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <X11/Xlib.h>

#ifndef BTN_SOUTH
#define BTN_SOUTH 0x130
#endif
#ifndef BTN_EAST
#define BTN_EAST 0x131
#endif
#ifndef BTN_NORTH
#define BTN_NORTH 0x133
#endif
#ifndef BTN_WEST
#define BTN_WEST 0x134
#endif

static bool zcsr_bit_is_set(const unsigned long* bits, unsigned code) {
    return (bits[code / (8U * (unsigned)sizeof(unsigned long))] &
            (1UL << (code % (8U * (unsigned)sizeof(unsigned long))))) != 0;
}

static float zcsr_norm_axis(int value) {
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    return value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
}

static float zcsr_norm_trigger(int value) {
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return (float)value / 255.0f;
}

static void zcsr_set_button(zcsr_pad_state* pad, zcsr_pad_button button, int down) {
    uint16_t bit;
    if (!pad || button < 0 || button >= ZCSR_PAD_BUTTON_COUNT) return;
    bit = (uint16_t)(1u << (unsigned)button);
    if (down) pad->buttons = (uint16_t)(pad->buttons | bit);
    else pad->buttons = (uint16_t)(pad->buttons & (uint16_t)~bit);
}

static bool zcsr_try_open_pad(zcsr_input_ext* in, int slot, int index) {
    char path[32];
    int fd;
    unsigned long evbits[2] = { 0, 0 };
    unsigned long keybits[(KEY_MAX / (8 * (int)sizeof(unsigned long))) + 1] = { 0 };
    if (!in || slot < 0 || slot >= ZCSR_PAD_MAX) return false;
    (void)snprintf(path, sizeof path, "/dev/input/event%d", index);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;
    if (ioctl(fd, EVIOCGBIT(0, (int)sizeof evbits), evbits) < 0 ||
        !zcsr_bit_is_set(evbits, EV_KEY) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, (int)sizeof keybits), keybits) < 0 ||
        (!zcsr_bit_is_set(keybits, BTN_GAMEPAD) && !zcsr_bit_is_set(keybits, BTN_SOUTH) &&
         !zcsr_bit_is_set(keybits, BTN_EAST))) {
        close(fd);
        return false;
    }
    in->gamepad_fd[slot] = fd;
    in->pads[slot].connected = true;
    return true;
}

static void zcsr_scan_gamepads(zcsr_input_ext* in) {
    int slot = 0;
    if (!in) return;
    for (int i = 0; i < ZCSR_PAD_MAX; ++i) in->gamepad_fd[i] = -1;
    for (int event = 0; event < 32 && slot < ZCSR_PAD_MAX; ++event) {
        if (zcsr_try_open_pad(in, slot, event)) ++slot;
    }
}

static void zcsr_close_gamepads(zcsr_input_ext* in) {
    if (!in) return;
    for (int i = 0; i < ZCSR_PAD_MAX; ++i) {
        if (in->gamepad_fd[i] >= 0) close(in->gamepad_fd[i]);
        in->gamepad_fd[i] = -1;
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

static void zcsr_apply_abs(zcsr_pad_state* pad, unsigned code, int value) {
    if (!pad) return;
    if (code == ABS_X) pad->lx = zcsr_norm_axis(value);
    else if (code == ABS_Y) pad->ly = zcsr_norm_axis(value);
    else if (code == ABS_RX) pad->rx = zcsr_norm_axis(value);
    else if (code == ABS_RY) pad->ry = zcsr_norm_axis(value);
    else if (code == ABS_Z) pad->lt = zcsr_norm_trigger(value);
    else if (code == ABS_RZ) pad->rt = zcsr_norm_trigger(value);
    else if (code == ABS_HAT0X) {
        zcsr_set_button(pad, ZCSR_PAD_DLEFT, value < 0);
        zcsr_set_button(pad, ZCSR_PAD_DRIGHT, value > 0);
    } else if (code == ABS_HAT0Y) {
        zcsr_set_button(pad, ZCSR_PAD_DUP, value < 0);
        zcsr_set_button(pad, ZCSR_PAD_DDOWN, value > 0);
    }
}

static void zcsr_apply_key(zcsr_pad_state* pad, unsigned code, int value) {
    int down = value != 0;
    if (!pad) return;
    if (code == BTN_SOUTH) zcsr_set_button(pad, ZCSR_PAD_A, down);
    else if (code == BTN_EAST) zcsr_set_button(pad, ZCSR_PAD_B, down);
    else if (code == BTN_WEST) zcsr_set_button(pad, ZCSR_PAD_X, down);
    else if (code == BTN_NORTH) zcsr_set_button(pad, ZCSR_PAD_Y, down);
    else if (code == BTN_TL) zcsr_set_button(pad, ZCSR_PAD_LB, down);
    else if (code == BTN_TR) zcsr_set_button(pad, ZCSR_PAD_RB, down);
    else if (code == BTN_SELECT) zcsr_set_button(pad, ZCSR_PAD_BACK, down);
    else if (code == BTN_START) zcsr_set_button(pad, ZCSR_PAD_START, down);
    else if (code == BTN_THUMBL) zcsr_set_button(pad, ZCSR_PAD_LSTICK, down);
    else if (code == BTN_THUMBR) zcsr_set_button(pad, ZCSR_PAD_RSTICK, down);
}

static void zcsr_pump_gamepads(zcsr_input_ext* in) {
    if (!in) return;
    for (int i = 0; i < ZCSR_PAD_MAX; ++i) {
        int fd = in->gamepad_fd[i];
        if (fd < 0) continue;
        for (;;) {
            struct input_event ev;
            ssize_t n = read(fd, &ev, sizeof ev);
            if (n == (ssize_t)sizeof ev) {
                if (ev.type == EV_ABS) zcsr_apply_abs(&in->pads[i], ev.code, ev.value);
                else if (ev.type == EV_KEY) zcsr_apply_key(&in->pads[i], ev.code, ev.value);
            } else {
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close(fd);
                    in->gamepad_fd[i] = -1;
                    in->pads[i] = (zcsr_pad_state){ 0 };
                }
                break;
            }
        }
    }
}

static void zcsr_select_mouse_events(zcsr_surface* window) {
    Display* display = (Display*)zcsr_surface_native_display(window);
    Window xwindow = (Window)(uintptr_t)zcsr_surface_native_handle(window);
    XWindowAttributes attrs;
    long mask;
    if (!display || !xwindow || !XGetWindowAttributes(display, xwindow, &attrs)) return;
    mask = attrs.your_event_mask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask |
           KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;
    XSelectInput(display, xwindow, mask);
}

static void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    Display* display = (Display*)zcsr_surface_native_display(window);
    Window xwindow = (Window)(uintptr_t)zcsr_surface_native_handle(window);
    XEvent ev;
    if (!in || !display || !xwindow) return;
    in->mouse.dx = 0;
    in->mouse.dy = 0;
    in->mouse.wheel = 0;
    while (XCheckTypedWindowEvent(display, xwindow, MotionNotify, &ev)) {
        int nx = ev.xmotion.x;
        int ny = ev.xmotion.y;
        in->mouse.dx += nx - in->mouse.x;
        in->mouse.dy += ny - in->mouse.y;
        in->mouse.x = nx;
        in->mouse.y = ny;
    }
    while (XCheckTypedWindowEvent(display, xwindow, ButtonPress, &ev)) {
        if (ev.xbutton.button == Button1) in->mouse.buttons |= ZCSR_MOUSE_LEFT;
        else if (ev.xbutton.button == Button2) in->mouse.buttons |= ZCSR_MOUSE_MIDDLE;
        else if (ev.xbutton.button == Button3) in->mouse.buttons |= ZCSR_MOUSE_RIGHT;
        else if (ev.xbutton.button == Button4) ++in->mouse.wheel;
        else if (ev.xbutton.button == Button5) --in->mouse.wheel;
        in->mouse.x = ev.xbutton.x;
        in->mouse.y = ev.xbutton.y;
    }
    while (XCheckTypedWindowEvent(display, xwindow, ButtonRelease, &ev)) {
        if (ev.xbutton.button == Button1) in->mouse.buttons &= (uint8_t)~ZCSR_MOUSE_LEFT;
        else if (ev.xbutton.button == Button2) in->mouse.buttons &= (uint8_t)~ZCSR_MOUSE_MIDDLE;
        else if (ev.xbutton.button == Button3) in->mouse.buttons &= (uint8_t)~ZCSR_MOUSE_RIGHT;
        in->mouse.x = ev.xbutton.x;
        in->mouse.y = ev.xbutton.y;
    }
}
#elif defined(_WIN32)

static bool zcsr_window_has_focus(HWND hwnd) {
    HWND foreground;

    if (!hwnd) return false;

    foreground = GetForegroundWindow();

    return foreground == hwnd ||
        (foreground != NULL &&
         IsChild(hwnd, foreground) != FALSE);
}

static float zcsr_norm_thumb(SHORT value) {
    if (value < 0) {
        return (float)value / 32768.0f;
    }

    return (float)value / 32767.0f;
}

static float zcsr_norm_trigger(BYTE value) {
    return (float)value / 255.0f;
}

static void zcsr_set_button(
    zcsr_pad_state* pad,
    zcsr_pad_button button,
    bool down ) {
    uint16_t bit;

    if (!pad) return;

    if ((unsigned)button >= (unsigned)ZCSR_PAD_BUTTON_COUNT) {
        return;
    }

    bit = (uint16_t)(1U << (unsigned)button);

    if (down) {
        pad->buttons = (uint16_t)(pad->buttons | bit);
    } else {
        pad->buttons = (uint16_t)(
            pad->buttons & (uint16_t)~bit
        );
    }
}

static void zcsr_scan_gamepads(zcsr_input_ext* in) {
    DWORD i;

    if (!in) return;

    for (i = 0; i < (DWORD)ZCSR_PAD_MAX; ++i) {
        XINPUT_STATE state;
        DWORD result;

        memset(&state, 0, sizeof(state));
        result = XInputGetState(i, &state);

        if (result != ERROR_SUCCESS) {
            in->pads[i] = (zcsr_pad_state){ 0 };
        } else {
            in->pads[i] = (zcsr_pad_state){ 0 };
            in->pads[i].connected = true;
        }
    }
}

static void zcsr_close_gamepads(zcsr_input_ext* in) {
    DWORD i;

    if (!in) return;

    for (i = 0; i < (DWORD)ZCSR_PAD_MAX; ++i) {
        in->pads[i] = (zcsr_pad_state){ 0 };
    }
}

static void zcsr_pump_gamepads(zcsr_input_ext* in) {
    DWORD i;

    if (!in) return;

    for (i = 0; i < (DWORD)ZCSR_PAD_MAX; ++i) {
        XINPUT_STATE state;
        zcsr_pad_state* pad = &in->pads[i];
        DWORD result;

        memset(&state, 0, sizeof(state));
        result = XInputGetState(i, &state);

        if (result != ERROR_SUCCESS) {
            *pad = (zcsr_pad_state){ 0 };
            continue;
        }

        pad->connected = true;
        pad->buttons = 0;

        pad->lx = zcsr_norm_thumb(state.Gamepad.sThumbLX);
        pad->ly = -zcsr_norm_thumb(state.Gamepad.sThumbLY);
        pad->rx = zcsr_norm_thumb(state.Gamepad.sThumbRX);
        pad->ry = -zcsr_norm_thumb(state.Gamepad.sThumbRY);

        pad->lt = zcsr_norm_trigger(state.Gamepad.bLeftTrigger);
        pad->rt = zcsr_norm_trigger(state.Gamepad.bRightTrigger);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0)
            zcsr_set_button(pad, ZCSR_PAD_A, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0)
            zcsr_set_button(pad, ZCSR_PAD_B, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0)
            zcsr_set_button(pad, ZCSR_PAD_X, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0)
            zcsr_set_button(pad, ZCSR_PAD_Y, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_LEFT_SHOULDER) != 0)
            zcsr_set_button(pad, ZCSR_PAD_LB, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0)
            zcsr_set_button(pad, ZCSR_PAD_RB, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0)
            zcsr_set_button(pad, ZCSR_PAD_BACK, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0)
            zcsr_set_button(pad, ZCSR_PAD_START, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_LEFT_THUMB) != 0)
            zcsr_set_button(pad, ZCSR_PAD_LSTICK, true);

        if ((state.Gamepad.wButtons &
            XINPUT_GAMEPAD_RIGHT_THUMB) != 0)
            zcsr_set_button(pad, ZCSR_PAD_RSTICK, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DUP, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DDOWN, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DLEFT, true);

        if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0)
            zcsr_set_button(pad, ZCSR_PAD_DRIGHT, true);
    }
}

static uint8_t zcsr_mouse_buttons(void) {
    uint8_t buttons = 0;
    SHORT state;

    state = GetAsyncKeyState(VK_LBUTTON);
    if ((state & 0x8000) != 0) {
        buttons = (uint8_t)(buttons | (uint8_t)ZCSR_MOUSE_LEFT);
    }

    state = GetAsyncKeyState(VK_RBUTTON);
    if ((state & 0x8000) != 0) {
        buttons = (uint8_t)(buttons | (uint8_t)ZCSR_MOUSE_RIGHT);
    }

    state = GetAsyncKeyState(VK_MBUTTON);
    if ((state & 0x8000) != 0) {
        buttons = (uint8_t)(buttons | (uint8_t)ZCSR_MOUSE_MIDDLE);
    }

    return buttons;
}

static int zcsr_clamp_mouse_x(int x, LONG width) {
    if (width <= 0) return 0;
    if (x < 0) return 0;
    if (x >= (int)width) return (int)width - 1;

    return x;
}

static int zcsr_clamp_mouse_y(int y, LONG height) {
    if (height <= 0) return 0;
    if (y < 0) return 0;
    if (y >= (int)height) return (int)height - 1;

    return y;
}

static void zcsr_accumulate_wheel(
    zcsr_input_ext* in,
    int delta ) {
    int total;
    int ticks;

    if (!in) return;

    total = in->wheel_remainder + delta;
    ticks = total / WHEEL_DELTA;
    in->wheel_remainder = total % WHEEL_DELTA;

    if (ticks > 0 && in->wheel_accum > INT_MAX - ticks) {
        in->wheel_accum = INT_MAX;
    } else if (ticks < 0 &&
               in->wheel_accum < INT_MIN - ticks) {
        in->wheel_accum = INT_MIN;
    } else {
        in->wheel_accum += ticks;
    }
}

static LRESULT CALLBACK zcsr_input_mouse_subclass_proc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR subclass_id,
    DWORD_PTR ref_data ) {
    zcsr_input_ext* in =
        (zcsr_input_ext*)(uintptr_t)ref_data;

    (void)subclass_id;
    (void)wparam;
    (void)lparam;

    /* --- mouse button events (event-driven edge detection) --- */
    if (in && in->mouse_subclass_hwnd == hwnd) {
        switch (message) {
            case WM_LBUTTONDOWN: in->mouse_btn_pressed  |= ZCSR_MOUSE_LEFT;  break;
            case WM_LBUTTONUP:   in->mouse_btn_released |= ZCSR_MOUSE_LEFT;  break;
            case WM_RBUTTONDOWN: in->mouse_btn_pressed  |= ZCSR_MOUSE_RIGHT; break;
            case WM_RBUTTONUP:   in->mouse_btn_released |= ZCSR_MOUSE_RIGHT; break;
            case WM_MBUTTONDOWN: in->mouse_btn_pressed  |= ZCSR_MOUSE_MIDDLE; break;
            case WM_MBUTTONUP:   in->mouse_btn_released |= ZCSR_MOUSE_MIDDLE; break;
        }
    }

    if (message == WM_MOUSEWHEEL &&
        in &&
        in->mouse_subclass_hwnd == hwnd &&
        zcsr_window_has_focus(hwnd)) {

        int delta = (int)(short)GET_WHEEL_DELTA_WPARAM(wparam);

        zcsr_accumulate_wheel(in, delta);
    }

    if (message == WM_NCDESTROY) {
        (void)RemoveWindowSubclass(
            hwnd,
            zcsr_input_mouse_subclass_proc,
            (UINT_PTR)(uintptr_t)in
        );

        if (in && in->mouse_subclass_hwnd == hwnd) {
            in->mouse_subclass_hwnd = NULL;
            in->mouse_inside = false;
            in->mouse_have_pos = false;
            in->wheel_accum = 0;
            in->wheel_remainder = 0;
        }
    }

    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static void zcsr_select_mouse_events(
    zcsr_input_ext* in,
    zcsr_surface* window ) {
    HWND hwnd = NULL;

    if (!in) return;

    if (window) {
        hwnd = (HWND)zcsr_surface_native_handle(window);
    }

    if (in->mouse_subclass_hwnd &&
        in->mouse_subclass_hwnd != hwnd) {

        (void)RemoveWindowSubclass(
            in->mouse_subclass_hwnd,
            zcsr_input_mouse_subclass_proc,
            (UINT_PTR)(uintptr_t)in
        );

        in->mouse_subclass_hwnd = NULL;
    }

    if (!hwnd) {
        in->mouse_inside = false;
        in->mouse_have_pos = false;
        in->mouse.buttons = 0;
        in->wheel_accum = 0;
        in->wheel_remainder = 0;
        return;
    }

    if (in->mouse_subclass_hwnd == hwnd) return;

    if (SetWindowSubclass(
            hwnd,
            zcsr_input_mouse_subclass_proc,
            (UINT_PTR)(uintptr_t)in,
            (DWORD_PTR)(uintptr_t)in)) {
        in->mouse_subclass_hwnd = hwnd;
    }
}

static void zcsr_pump_mouse(
    zcsr_input_ext* in,
    zcsr_surface* window ) {
    HWND hwnd;
    POINT pt;
    RECT client;
    LONG width;
    LONG height;
    bool inside;
    int x;
    int y;

    if (!in) return;

    in->mouse.dx = 0;
    in->mouse.dy = 0;

    in->mouse.wheel = in->wheel_accum;
    in->wheel_accum = 0;

    in->mouse.buttons = 0;

    if (!window) {
        in->mouse_inside = false;
        in->mouse_btn_pressed = 0;
        in->mouse_btn_released = 0;
        return;
    }

    hwnd = (HWND)zcsr_surface_native_handle(window);
    if (!hwnd) {
        in->mouse_inside = false;
        in->mouse_btn_pressed = 0;
        in->mouse_btn_released = 0;
        return;
    }

    bool has_focus = zcsr_window_has_focus(hwnd);

    /* Always sample mouse position (even without focus) so hover tracking
     * works when the user clicks the window to focus it. */
    if (!GetClientRect(hwnd, &client)) {
        in->mouse_inside = false;
        return;
    }

    width = client.right - client.left;
    height = client.bottom - client.top;

    if (width <= 0 || height <= 0) {
        in->mouse_inside = false;
        in->mouse_have_pos = false;
        return;
    }

    if (!GetCursorPos(&pt) || !ScreenToClient(hwnd, &pt)) {
        in->mouse_inside = false;
        return;
    }

    inside = PtInRect(&client, pt) != FALSE;

    x = zcsr_clamp_mouse_x(pt.x, width);
    y = zcsr_clamp_mouse_y(pt.y, height);

    if (!in->mouse_have_pos) {
        in->mouse.x = x;
        in->mouse.y = y;
        in->mouse_have_pos = true;
        in->mouse_inside = inside;
    } else {
        if (inside && in->mouse_inside) {
            in->mouse.dx = x - in->mouse.x;
            in->mouse.dy = y - in->mouse.y;
        }
        in->mouse.x = x;
        in->mouse.y = y;
        in->mouse_inside = inside;
    }

    /* Button state: only report when focused, but merge event-driven edges
     * so a click that focuses the window is not lost. */
    if (has_focus) {
        in->mouse.buttons = zcsr_mouse_buttons() | in->mouse_btn_pressed;
    } else {
        /* If a press event arrived while unfocused, the window is likely
         * being clicked to focus — report it so the UI can react. */
        if (in->mouse_btn_pressed != 0) {
            in->mouse.buttons = in->mouse_btn_pressed;
        }
    }

    /* Clear edge events after each pump (consumed). */
    in->mouse_btn_pressed = 0;
    in->mouse_btn_released = 0;
}

#else

static void zcsr_scan_gamepads(zcsr_input_ext* in) { (void)in; }
static void zcsr_close_gamepads(zcsr_input_ext* in) { (void)in; }
static void zcsr_select_mouse_events(zcsr_surface* window) { (void)window; }
static void zcsr_pump_gamepads(zcsr_input_ext* in) { (void)in; }
static void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    if (in) {
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
    }
    (void)window;
}
#endif

zcsr_input_ext* zcsr_input_ext_init(void* buffer, size_t bytes) {
    zcsr_input_ext* in;
    if (!buffer || bytes < sizeof(zcsr_input_ext)) return 0;
    in = (zcsr_input_ext*)buffer;
    *in = (zcsr_input_ext){ 0 };
    zcsr_scan_gamepads(in);
    return in;
}

void zcsr_input_ext_shutdown(zcsr_input_ext* in) {
    if (!in) return;

#if defined(_WIN32)
    zcsr_select_mouse_events(in, NULL);
#endif

    zcsr_close_gamepads(in);
    *in = (zcsr_input_ext){ 0 };
}

void zcsr_input_ext_pump(
    zcsr_input_ext* in,
    zcsr_surface* window ) {
    if (!in) return;

    if (window && (!in->initialized || in->surface != window)) {
#if defined(_WIN32)
        zcsr_select_mouse_events(in, window);
#else
        zcsr_select_mouse_events(window);
#endif

        in->surface = window;
        in->initialized = true;
    }

    if (window) {
        zcsr_pump_mouse(in, window);
    } else {
#if defined(_WIN32)
        zcsr_select_mouse_events(in, NULL);
        in->surface = NULL;
        in->initialized = false;
#endif
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
        in->mouse.buttons = 0;
    }

    zcsr_pump_gamepads(in);
#if defined(__ANDROID__)
    in->touch = in->touch_pending;
    in->touch_pending.pressed = false;
    in->touch_pending.released = false;
    in->touch_pending.dx = 0.0f;
    in->touch_pending.dy = 0.0f;
#endif
}

zcsr_mouse_state zcsr_input_ext_mouse(const zcsr_input_ext* in) {
    return in ? in->mouse : (zcsr_mouse_state){ 0, 0, 0, 0, 0, 0 };
}

zcsr_pad_state zcsr_input_ext_pad(const zcsr_input_ext* in, int index) {
    if (!in || index < 0 || index >= ZCSR_PAD_MAX) return (zcsr_pad_state){ 0 };
    return in->pads[index];
}

zcsr_touch_state zcsr_input_ext_touch(const zcsr_input_ext* in) {
#if defined(__ANDROID__)
    return in ? in->touch : (zcsr_touch_state){ 0 };
#else
    (void)in;
    return (zcsr_touch_state){ 0 };
#endif
}
