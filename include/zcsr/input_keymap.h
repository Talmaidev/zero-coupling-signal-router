#ifndef ZCSR_INPUT_KEYMAP_H
#define ZCSR_INPUT_KEYMAP_H
/* Optional native-code -> ZCSR_KEY_* conversion table.
 *
 * The engine backends use this internally; applications do not need to call it.
 * It is exposed as a separate opt-in header for tests, downstream migration
 * tools and platform bridges that already receive a native code.
 *
 * `native_code` is interpreted according to `domain`:
 *   WIN32_VK       Windows virtual-key code (WM_KEYDOWN wParam)
 *   X11_KEYCODE    X11 keycode (evdev code + 8, XKeyEvent.keycode)
 *   EVDEV          Linux input-event-code (Wayland wl_keyboard.key)
 *   ANDROID_KEYCODE Android AKeyEvent_getKeyCode()
 *   COCOA_KEYCODE   macOS NSEvent.keyCode
 */
#include <stdint.h>
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZCSR_KEY_DOMAIN_WIN32_VK = 0,
    ZCSR_KEY_DOMAIN_X11_KEYCODE = 1,
    ZCSR_KEY_DOMAIN_EVDEV = 2,
    ZCSR_KEY_DOMAIN_ANDROID_KEYCODE = 3,
    ZCSR_KEY_DOMAIN_COCOA_KEYCODE = 4
} zcsr_key_domain;

/* Returns ZCSR_KEY_NONE when the native code has no common-key equivalent. */
zcsr_keycode zcsr_keycode_from_native(zcsr_key_domain domain, uint32_t native_code);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_INPUT_KEYMAP_H */
