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
    void* mouse_subclass_hwnd;  /* HWND */
    int wheel_accum;
    int wheel_remainder;
    uint8_t mouse_btn_pressed;
    uint8_t mouse_btn_released;
#endif
};

/* Internal entry points — implemented per-backend in inputext.c / inputext_wayland.c.
 * De-static so multiple translation units can share the interface. */
void zcsr_scan_gamepads(zcsr_input_ext* in);
void zcsr_close_gamepads(zcsr_input_ext* in);
void zcsr_pump_gamepads(zcsr_input_ext* in);

#endif /* ZCSR_INPUTEXT_INTERNAL_H */
