/* Agent 1 — Cocoa (macOS) key-event pump. Objective-C, compiled only on Apple.
 * Implements the C-ABI shim in input_cocoa.h. Includes only the same-module shim header and the
 * system Cocoa framework — no cross-module includes (zero-coupling preserved).
 *
 * Native NSEvent.keyCode is translated by the common input_keymap table; the
 * caller never sees a raw Apple keycode. Modifier keys arrive through
 * NSEventMaskFlagsChanged (macOS does not reliably emit KeyDown/KeyUp for
 * Shift/Ctrl/Option/Command), and are forwarded as the same physical-key
 * down/up edges as every other backend.
 *
 * NOTE: written without an Apple toolchain — NOT compiled here. Needs a Mac to build/verify.
 * Event-pump ownership: zcsr_surface_pump_events (platform) drains all NON-key events and LEAVES
 * key down/up in the queue, which this function consumes — so a typical "surface pump then input
 * pump" loop delivers keys correctly. */
#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "input_cocoa.h"

static int zcsr_cocoa_is_modifier_keycode(uint16_t keycode) {
    switch (keycode) {
    case 54: case 55: /* right/left Command */
    case 56: case 60: /* left/right Shift */
    case 57:          /* Caps Lock */
    case 58: case 61: /* left/right Option */
    case 59: case 62: /* left/right Control */
        return 1;
    default:
        return 0;
    }
}

static int zcsr_cocoa_modifier_is_down(NSEvent* event, uint16_t keycode) {
    NSEventModifierFlags flags = [event modifierFlags];
    switch (keycode) {
    case 54: case 55: return (flags & NSEventModifierFlagCommand) != 0;
    case 56: case 60: return (flags & NSEventModifierFlagShift) != 0;
    case 57:          return (flags & NSEventModifierFlagCapsLock) != 0;
    case 58: case 61: return (flags & NSEventModifierFlagOption) != 0;
    case 59: case 62: return (flags & NSEventModifierFlagControl) != 0;
    default:          return 0;
    }
}

void zcsr_cocoa_input_pump(void* ns_window, zcsr_cocoa_key_cb cb, void* ctx, int* out_close) {
    if (out_close) *out_close = 0;
    @autoreleasepool {
        NSWindow* window = ns_window ? (__bridge NSWindow*)ns_window : nil;
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp |
                                                       NSEventMaskFlagsChanged)
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil) {
            NSEventType type = [event type];
            uint16_t keycode = (uint16_t)[event keyCode];

            if (type == NSEventTypeFlagsChanged) {
                if (cb) cb(ctx, keycode, zcsr_cocoa_modifier_is_down(event, keycode));
                continue;
            }

            /* Some keyboards also emit KeyDown/KeyUp for a modifier. The
             * flagsChanged edge is authoritative; drop the duplicate. */
            if (zcsr_cocoa_is_modifier_keycode(keycode)) continue;

            int is_down = (type == NSEventTypeKeyDown) ? 1 : 0;
            if (is_down && [event isARepeat]) continue; /* edge semantics: ignore auto-repeat */
            if (cb) cb(ctx, keycode, is_down);
        }
        if (out_close && window && ![window isVisible]) *out_close = 1;
    }
}

#endif /* __APPLE__ */
