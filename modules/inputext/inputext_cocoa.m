/* inputext_cocoa.m — real Cocoa mouse/text and GameController backend.
 *
 * input.c owns keyboard state; this file installs a local NSEvent monitor for
 * mouse and text (physical keyboard characters). It does not consume events:
 * the monitor returns the event so the rest of the app still receives it.
 *
 * IME/marked text needs NSTextInputClient on a custom NSView and is not
 * silently claimed here. Gamepad uses GameController.framework's polled
 * controller list, equivalent to XInput/evdev/Android joystick.
 */
#if defined(__APPLE__)

#include "inputext_internal.h"

#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <string.h>

static zcsr_input_ext* g_cocoa_owner = NULL;
static id g_cocoa_monitor = nil;
static id g_cocoa_resign_observer = nil;

static int zcsr_cocoa_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint8_t zcsr_cocoa_mods(NSEvent* event) {
    NSEventModifierFlags flags = event ? [event modifierFlags] : 0;
    uint8_t mods = 0;
    if ((flags & NSEventModifierFlagControl) != 0) mods |= ZCSR_MOD_CTRL;
    if ((flags & NSEventModifierFlagShift) != 0) mods |= ZCSR_MOD_SHIFT;
    if ((flags & NSEventModifierFlagOption) != 0) mods |= ZCSR_MOD_ALT;
    if ((flags & NSEventModifierFlagCommand) != 0) mods |= ZCSR_MOD_GUI;
    return mods;
}

static void zcsr_cocoa_update_mouse_position(zcsr_input_ext* in, NSEvent* event) {
    NSWindow* win;
    NSView* view;
    NSPoint p;
    NSPoint backing_p;
    NSRect bounds;
    NSRect backing_bounds;
    if (!in || !event) return;
    win = (NSWindow*)in->ns_window;
    if (!win) return;
    view = [win contentView];
    if (!view) return;
    bounds = [view bounds];
    backing_bounds = [view convertRectToBacking:bounds];
    p = [view convertPoint:[event locationInWindow] fromView:nil];
    backing_p = [view convertPointToBacking:p];
    in->mouse.x = zcsr_cocoa_clamp((int)backing_p.x, 0,
                                   (int)backing_bounds.size.width - 1);
    in->mouse.y = zcsr_cocoa_clamp((int)(backing_bounds.size.height - backing_p.y), 0,
                                   (int)backing_bounds.size.height - 1);
    in->mouse_have_pos = true;
}

static void zcsr_cocoa_set_button(zcsr_input_ext* in, uint8_t bit, bool down) {
    if (!in) return;
    if (down) {
        in->mouse.buttons = (uint8_t)(in->mouse.buttons | bit);
        in->mouse_btn_pressed = (uint8_t)(in->mouse_btn_pressed | bit);
    } else {
        in->mouse.buttons = (uint8_t)(in->mouse.buttons & (uint8_t)~bit);
        in->mouse_btn_released = (uint8_t)(in->mouse_btn_released | bit);
    }
}

static NSEvent* zcsr_cocoa_event_handler(NSEvent* event) {
    zcsr_input_ext* in = g_cocoa_owner;
    NSWindow* target;
    if (!in || !event) return event;
    target = (NSWindow*)in->ns_window;
    if (target && event.window && event.window != target) return event;

    switch (event.type) {
    case NSEventTypeLeftMouseDown:
        zcsr_cocoa_update_mouse_position(in, event);
        zcsr_cocoa_set_button(in, ZCSR_MOUSE_LEFT, true);
        zcsr_mouse_record_left_down(in, in->mouse.x, in->mouse.y,
                                    zcsr_cocoa_mods(event));
        break;
    case NSEventTypeLeftMouseUp:
        zcsr_cocoa_update_mouse_position(in, event);
        zcsr_cocoa_set_button(in, ZCSR_MOUSE_LEFT, false);
        zcsr_mouse_clear_down(in);
        break;
    case NSEventTypeRightMouseDown:
        zcsr_cocoa_update_mouse_position(in, event);
        zcsr_cocoa_set_button(in, ZCSR_MOUSE_RIGHT, true);
        break;
    case NSEventTypeRightMouseUp:
        zcsr_cocoa_update_mouse_position(in, event);
        zcsr_cocoa_set_button(in, ZCSR_MOUSE_RIGHT, false);
        break;
    case NSEventTypeOtherMouseDown:
        if ([event buttonNumber] == 2) {
            zcsr_cocoa_update_mouse_position(in, event);
            zcsr_cocoa_set_button(in, ZCSR_MOUSE_MIDDLE, true);
        }
        break;
    case NSEventTypeOtherMouseUp:
        if ([event buttonNumber] == 2) {
            zcsr_cocoa_update_mouse_position(in, event);
            zcsr_cocoa_set_button(in, ZCSR_MOUSE_MIDDLE, false);
        }
        break;
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged: {
        int old_x = in->mouse.x;
        int old_y = in->mouse.y;
        bool had_pos = in->mouse_have_pos;
        zcsr_cocoa_update_mouse_position(in, event);
        if (had_pos) {
            in->mouse_dx_accum += in->mouse.x - old_x;
            in->mouse_dy_accum += in->mouse.y - old_y;
        }
        break;
    }
    case NSEventTypeScrollWheel:
        in->wheel_accum += (int)[event scrollingDeltaY];
        break;
    case NSEventTypeKeyDown: {
        NSString* chars = [event characters];
        NSUInteger len = [chars length];
        if (len > 0 &&
            ([event modifierFlags] & (NSEventModifierFlagCommand |
                                      NSEventModifierFlagControl)) == 0) {
            const char* utf8 = [chars UTF8String];
            if (utf8 && utf8[0] != '\0') {
                zcsr_input_text_push_utf8(in, utf8, strlen(utf8));
            }
        }
        break;
    }
    default:
        break;
    }
    return event;
}

static void zcsr_cocoa_remove_monitor(zcsr_input_ext* in) {
    if (g_cocoa_resign_observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:g_cocoa_resign_observer];
        g_cocoa_resign_observer = nil;
    }
    if (g_cocoa_monitor) {
        [NSEvent removeMonitor:g_cocoa_monitor];
        g_cocoa_monitor = nil;
    }
    if (in) in->event_monitor = NULL;
    g_cocoa_owner = NULL;
}

void zcsr_select_mouse_events(zcsr_input_ext* in, zcsr_surface* window) {
    NSWindow* win;
    if (!in) return;
    if (!window) {
        zcsr_cocoa_remove_monitor(in);
        if (in->ns_window) in->ns_window = NULL;
        return;
    }
    win = (NSWindow*)zcsr_surface_native_handle(window);
    if (!win) return;
    if (in->event_monitor && in->ns_window == (__bridge void*)win) return;

    zcsr_cocoa_remove_monitor(in);
    in->ns_window = (__bridge void*)win;
    g_cocoa_owner = in;
    g_cocoa_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:
        (NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp |
         NSEventMaskRightMouseDown | NSEventMaskRightMouseUp |
         NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp |
         NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged |
         NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged |
         NSEventMaskScrollWheel | NSEventMaskKeyDown)
        handler:^NSEvent*(NSEvent* event) {
            return zcsr_cocoa_event_handler(event);
        }];
    in->event_monitor = (__bridge void*)g_cocoa_monitor;

    /* Focus loss can happen without a mouse-up reaching this process. Clear
     * the held mask and press origin so the next window does not inherit them. */
    g_cocoa_resign_observer =
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidResignKeyNotification
                                                          object:win
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification* note) {
            (void)note;
            if (g_cocoa_owner) {
                g_cocoa_owner->mouse.buttons = 0;
                zcsr_mouse_clear_down(g_cocoa_owner);
            }
        }];
}

void zcsr_pump_mouse(zcsr_input_ext* in, zcsr_surface* window) {
    if (!in) return;
    if (!window || !in->ns_window) {
        in->mouse.dx = 0;
        in->mouse.dy = 0;
        in->mouse.wheel = 0;
        in->mouse_dx_accum = 0;
        in->mouse_dy_accum = 0;
        in->wheel_accum = 0;
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

/* ---- GameController ---- */
void zcsr_scan_gamepads(zcsr_input_ext* in) {
    int i;
    if (!in) return;
    for (i = 0; i < ZCSR_PAD_MAX; ++i) in->pads[i] = (zcsr_pad_state){ 0 };
}

void zcsr_close_gamepads(zcsr_input_ext* in) {
    zcsr_scan_gamepads(in);
}

static void zcsr_cocoa_pad_button(zcsr_pad_state* pad, zcsr_pad_button button, bool down) {
    uint16_t bit;
    if (!pad || (unsigned)button >= (unsigned)ZCSR_PAD_BUTTON_COUNT) return;
    bit = (uint16_t)(1u << (unsigned)button);
    if (down) pad->buttons = (uint16_t)(pad->buttons | bit);
    else pad->buttons = (uint16_t)(pad->buttons & (uint16_t)~bit);
}

void zcsr_pump_gamepads(zcsr_input_ext* in) {
    NSArray<GCController*>* controllers;
    NSUInteger count;
    NSUInteger i;
    if (!in) return;
    controllers = [GCController controllers];
    count = [controllers count];
    for (i = 0; i < ZCSR_PAD_MAX; ++i) {
        zcsr_pad_state* pad = &in->pads[i];
        *pad = (zcsr_pad_state){ 0 };
        if (i >= count) continue;
        GCController* controller = [controllers objectAtIndex:i];
        GCExtendedGamepad* gp = controller.extendedGamepad;
        if (!gp) continue;
        pad->connected = true;
        pad->lx = gp.leftThumbstick.xAxis.value;
        pad->ly = gp.leftThumbstick.yAxis.value;
        pad->rx = gp.rightThumbstick.xAxis.value;
        pad->ry = gp.rightThumbstick.yAxis.value;
        pad->lt = gp.leftTrigger.value;
        pad->rt = gp.rightTrigger.value;
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_A, gp.buttonA.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_B, gp.buttonB.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_X, gp.buttonX.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_Y, gp.buttonY.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_LB, gp.leftShoulder.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_RB, gp.rightShoulder.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_LSTICK, gp.leftThumbstickButton.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_RSTICK, gp.rightThumbstickButton.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_DUP, gp.dpad.up.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_DDOWN, gp.dpad.down.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_DLEFT, gp.dpad.left.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_DRIGHT, gp.dpad.right.isPressed);
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_START, gp.buttonMenu.isPressed);
        /* GameController has no dedicated Back/Select on every profile;
         * buttonOptions maps to Back when present. */
        zcsr_cocoa_pad_button(pad, ZCSR_PAD_BACK, gp.buttonOptions.isPressed);
    }
}

#endif /* __APPLE__ */

typedef struct zcsr_inputext_cocoa_tu_anchor { int reserved; } zcsr_inputext_cocoa_tu_anchor;
