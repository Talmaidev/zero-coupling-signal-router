#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include "zcsr/input_keymap.h"

static zcsr_keycode win32(uint32_t code) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_WIN32_VK, code);
}
static zcsr_keycode x11(uint32_t code) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_X11_KEYCODE, code);
}
static zcsr_keycode evdev(uint32_t code) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_EVDEV, code);
}
static zcsr_keycode android(uint32_t code) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_ANDROID_KEYCODE, code);
}
static zcsr_keycode cocoa(uint32_t code) {
    return zcsr_keycode_from_native(ZCSR_KEY_DOMAIN_COCOA_KEYCODE, code);
}

int main(void) {
    /* The same physical key must resolve to the same engine code on every host. */
    assert(win32(0x1B) == ZCSR_KEY_ESCAPE);
    assert(x11(9) == ZCSR_KEY_ESCAPE);
    assert(evdev(1) == ZCSR_KEY_ESCAPE);
    assert(android(111) == ZCSR_KEY_ESCAPE);
    assert(cocoa(53) == ZCSR_KEY_ESCAPE);

    assert(win32(0x20) == ZCSR_KEY_SPACE);
    assert(x11(65) == ZCSR_KEY_SPACE);
    assert(evdev(57) == ZCSR_KEY_SPACE);
    assert(android(62) == ZCSR_KEY_SPACE);
    assert(cocoa(49) == ZCSR_KEY_SPACE);

    assert(win32(0x09) == ZCSR_KEY_TAB);
    assert(x11(23) == ZCSR_KEY_TAB);
    assert(evdev(15) == ZCSR_KEY_TAB);
    assert(android(61) == ZCSR_KEY_TAB);
    assert(cocoa(48) == ZCSR_KEY_TAB);

    assert(win32(0x0D) == ZCSR_KEY_ENTER);
    assert(x11(36) == ZCSR_KEY_ENTER);
    assert(evdev(28) == ZCSR_KEY_ENTER);
    assert(android(66) == ZCSR_KEY_ENTER);
    assert(cocoa(36) == ZCSR_KEY_ENTER);

    assert(win32(0x41) == ZCSR_KEY_A);
    assert(x11(38) == ZCSR_KEY_A);
    assert(evdev(30) == ZCSR_KEY_A);
    assert(android(29) == ZCSR_KEY_A);
    assert(cocoa(0) == ZCSR_KEY_A);

    /* Every Linux letter position, not just A. Evdev codes are QWERTY
     * physical positions; a Q+offset/A+offset shortcut remaps the whole row. */
    {
        static const uint32_t evdev_letters[26] = {
            16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u,
            30u, 31u, 32u, 33u, 34u, 35u, 36u, 37u, 38u,
            44u, 45u, 46u, 47u, 48u, 49u, 50u
        };
        static const zcsr_keycode letters[26] = {
            ZCSR_KEY_Q, ZCSR_KEY_W, ZCSR_KEY_E, ZCSR_KEY_R, ZCSR_KEY_T,
            ZCSR_KEY_Y, ZCSR_KEY_U, ZCSR_KEY_I, ZCSR_KEY_O, ZCSR_KEY_P,
            ZCSR_KEY_A, ZCSR_KEY_S, ZCSR_KEY_D, ZCSR_KEY_F, ZCSR_KEY_G,
            ZCSR_KEY_H, ZCSR_KEY_J, ZCSR_KEY_K, ZCSR_KEY_L,
            ZCSR_KEY_Z, ZCSR_KEY_X, ZCSR_KEY_C, ZCSR_KEY_V,
            ZCSR_KEY_B, ZCSR_KEY_N, ZCSR_KEY_M
        };
        for (size_t i = 0; i < 26u; ++i) {
            assert(evdev(evdev_letters[i]) == letters[i]);
            assert(x11(evdev_letters[i] + 8u) == letters[i]);
        }
    }

    assert(win32(0x30) == ZCSR_KEY_0);
    assert(x11(19) == ZCSR_KEY_0);
    assert(evdev(11) == ZCSR_KEY_0);
    assert(android(7) == ZCSR_KEY_0);
    assert(cocoa(29) == ZCSR_KEY_0);

    assert(win32(0x10) == ZCSR_KEY_SHIFT);
    assert(win32(0x11) == ZCSR_KEY_CTRL);
    assert(win32(0x12) == ZCSR_KEY_ALT);
    assert(win32(0x5B) == ZCSR_KEY_GUI);
    assert(evdev(42) == ZCSR_KEY_SHIFT);
    assert(evdev(29) == ZCSR_KEY_CTRL);
    assert(evdev(56) == ZCSR_KEY_ALT);
    assert(evdev(125) == ZCSR_KEY_GUI);
    assert(android(59) == ZCSR_KEY_SHIFT);
    assert(android(113) == ZCSR_KEY_CTRL);
    assert(android(57) == ZCSR_KEY_ALT);
    assert(android(117) == ZCSR_KEY_GUI);
    assert(cocoa(56) == ZCSR_KEY_SHIFT);
    assert(cocoa(59) == ZCSR_KEY_CTRL);
    assert(cocoa(58) == ZCSR_KEY_ALT);
    assert(cocoa(55) == ZCSR_KEY_GUI);

    assert(win32(0x25) == ZCSR_KEY_ARROW_LEFT);
    assert(win32(0x27) == ZCSR_KEY_ARROW_RIGHT);
    assert(win32(0x26) == ZCSR_KEY_ARROW_UP);
    assert(win32(0x28) == ZCSR_KEY_ARROW_DOWN);
    assert(evdev(105) == ZCSR_KEY_ARROW_LEFT);
    assert(evdev(106) == ZCSR_KEY_ARROW_RIGHT);
    assert(evdev(103) == ZCSR_KEY_ARROW_UP);
    assert(evdev(108) == ZCSR_KEY_ARROW_DOWN);
    assert(android(21) == ZCSR_KEY_ARROW_LEFT);
    assert(android(22) == ZCSR_KEY_ARROW_RIGHT);
    assert(android(19) == ZCSR_KEY_ARROW_UP);
    assert(android(20) == ZCSR_KEY_ARROW_DOWN);
    assert(cocoa(123) == ZCSR_KEY_ARROW_LEFT);
    assert(cocoa(124) == ZCSR_KEY_ARROW_RIGHT);
    assert(cocoa(126) == ZCSR_KEY_ARROW_UP);
    assert(cocoa(125) == ZCSR_KEY_ARROW_DOWN);

    assert(win32(0x08) == ZCSR_KEY_BACKSPACE);
    assert(win32(0x2E) == ZCSR_KEY_DELETE);
    assert(evdev(14) == ZCSR_KEY_BACKSPACE);
    assert(evdev(111) == ZCSR_KEY_DELETE);
    assert(android(67) == ZCSR_KEY_BACKSPACE);
    assert(android(112) == ZCSR_KEY_DELETE);
    assert(cocoa(51) == ZCSR_KEY_BACKSPACE);
    assert(cocoa(117) == ZCSR_KEY_DELETE);

    assert(win32(0x70) == ZCSR_KEY_F1);
    assert(win32(0x7B) == ZCSR_KEY_F12);
    assert(evdev(59) == ZCSR_KEY_F1);
    assert(evdev(88) == ZCSR_KEY_F12);
    assert(android(131) == ZCSR_KEY_F1);
    assert(android(142) == ZCSR_KEY_F12);
    assert(cocoa(122) == ZCSR_KEY_F1);
    assert(cocoa(111) == ZCSR_KEY_F12);

    assert(win32(0x00) == ZCSR_KEY_NONE);
    assert(x11(0) == ZCSR_KEY_NONE);
    assert(evdev(0) == ZCSR_KEY_NONE);
    assert(android(0) == ZCSR_KEY_NONE);
    assert(cocoa(255) == ZCSR_KEY_NONE);

    puts("input_keymap: ok");
    return 0;
}
