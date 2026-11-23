/* Pure native-code -> ZCSR_KEY_* tables. No platform headers, so this file is
 * testable on every host and can be shared by downsteam migration tools. */
#include "zcsr/input_keymap.h"

static zcsr_keycode from_evdev(uint32_t code) {
    /* Evdev letter codes are physical QWERTY positions, not alphabetical.
     * 16..25 = QWERTY top row, 30..38 = home row, 44..50 = bottom row.
     * Treating them as Q+offset/A+offset/Z+offset silently remaps every Linux
     * letter (O became Y, S became B), which kills Ctrl+O/S shortcuts and game
     * input even though the text callback still looks correct. */
    static const zcsr_keycode top_row[10] = {
        ZCSR_KEY_Q, ZCSR_KEY_W, ZCSR_KEY_E, ZCSR_KEY_R, ZCSR_KEY_T,
        ZCSR_KEY_Y, ZCSR_KEY_U, ZCSR_KEY_I, ZCSR_KEY_O, ZCSR_KEY_P
    };
    static const zcsr_keycode home_row[9] = {
        ZCSR_KEY_A, ZCSR_KEY_S, ZCSR_KEY_D, ZCSR_KEY_F, ZCSR_KEY_G,
        ZCSR_KEY_H, ZCSR_KEY_J, ZCSR_KEY_K, ZCSR_KEY_L
    };
    static const zcsr_keycode bottom_row[7] = {
        ZCSR_KEY_Z, ZCSR_KEY_X, ZCSR_KEY_C, ZCSR_KEY_V,
        ZCSR_KEY_B, ZCSR_KEY_N, ZCSR_KEY_M
    };
    if (code >= 16u && code <= 25u) return top_row[code - 16u];
    if (code >= 30u && code <= 38u) return home_row[code - 30u];
    if (code >= 44u && code <= 50u) return bottom_row[code - 44u];
    if (code >= 2u && code <= 11u) {
        uint32_t digit = (code == 11u) ? 0u : (code - 1u);
        return (zcsr_keycode)(ZCSR_KEY_0 + digit);
    }
    if (code >= 59u && code <= 68u) return (zcsr_keycode)(ZCSR_KEY_F1 + (code - 59u));
    switch (code) {
    case 1u:   return ZCSR_KEY_ESCAPE;
    case 12u:  return ZCSR_KEY_MINUS;
    case 13u:  return ZCSR_KEY_EQUALS;
    case 14u:  return ZCSR_KEY_BACKSPACE;
    case 15u:  return ZCSR_KEY_TAB;
    case 26u:  return ZCSR_KEY_LEFT_BRACKET;
    case 27u:  return ZCSR_KEY_RIGHT_BRACKET;
    case 28u:  return ZCSR_KEY_ENTER;
    case 29u:  return ZCSR_KEY_CTRL;
    case 39u:  return ZCSR_KEY_SEMICOLON;
    case 40u:  return ZCSR_KEY_APOSTROPHE;
    case 41u:  return ZCSR_KEY_GRAVE;
    case 42u:  return ZCSR_KEY_SHIFT;
    case 43u:  return ZCSR_KEY_BACKSLASH;
    case 51u:  return ZCSR_KEY_COMMA;
    case 52u:  return ZCSR_KEY_PERIOD;
    case 53u:  return ZCSR_KEY_SLASH;
    case 54u:  return ZCSR_KEY_SHIFT;
    case 55u:  return ZCSR_KEY_KP_MULTIPLY;
    case 56u:  return ZCSR_KEY_ALT;
    case 57u:  return ZCSR_KEY_SPACE;
    case 58u:  return ZCSR_KEY_CAPS_LOCK;
    case 69u:  return ZCSR_KEY_NUM_LOCK;
    case 70u:  return ZCSR_KEY_SCROLL_LOCK;
    case 71u:  return ZCSR_KEY_KP_7;
    case 72u:  return ZCSR_KEY_KP_8;
    case 73u:  return ZCSR_KEY_KP_9;
    case 74u:  return ZCSR_KEY_KP_MINUS;
    case 75u:  return ZCSR_KEY_KP_4;
    case 76u:  return ZCSR_KEY_KP_5;
    case 77u:  return ZCSR_KEY_KP_6;
    case 78u:  return ZCSR_KEY_KP_PLUS;
    case 79u:  return ZCSR_KEY_KP_1;
    case 80u:  return ZCSR_KEY_KP_2;
    case 81u:  return ZCSR_KEY_KP_3;
    case 82u:  return ZCSR_KEY_KP_0;
    case 83u:  return ZCSR_KEY_KP_DECIMAL;
    case 87u:  return ZCSR_KEY_F11;
    case 88u:  return ZCSR_KEY_F12;
    case 96u:  return ZCSR_KEY_KP_ENTER;
    case 97u:  return ZCSR_KEY_CTRL;
    case 98u:  return ZCSR_KEY_KP_DIVIDE;
    case 99u:  return ZCSR_KEY_PRINT_SCREEN;
    case 100u: return ZCSR_KEY_ALT;
    case 102u: return ZCSR_KEY_HOME;
    case 103u: return ZCSR_KEY_ARROW_UP;
    case 104u: return ZCSR_KEY_PAGE_UP;
    case 105u: return ZCSR_KEY_ARROW_LEFT;
    case 106u: return ZCSR_KEY_ARROW_RIGHT;
    case 107u: return ZCSR_KEY_END;
    case 108u: return ZCSR_KEY_ARROW_DOWN;
    case 109u: return ZCSR_KEY_PAGE_DOWN;
    case 110u: return ZCSR_KEY_INSERT;
    case 111u: return ZCSR_KEY_DELETE;
    case 119u: return ZCSR_KEY_PAUSE;
    case 125u: return ZCSR_KEY_GUI;
    case 126u: return ZCSR_KEY_GUI;
    case 127u: return ZCSR_KEY_MENU;
    case 139u: return ZCSR_KEY_MENU;
    default:   return ZCSR_KEY_NONE;
    }
}

static zcsr_keycode from_win32(uint32_t vk) {
    if (vk >= 0x41u && vk <= 0x5Au) return (zcsr_keycode)(ZCSR_KEY_A + (vk - 0x41u));
    if (vk >= 0x30u && vk <= 0x39u) return (zcsr_keycode)(ZCSR_KEY_0 + (vk - 0x30u));
    if (vk >= 0x70u && vk <= 0x7Bu) return (zcsr_keycode)(ZCSR_KEY_F1 + (vk - 0x70u));
    if (vk >= 0x60u && vk <= 0x69u) return (zcsr_keycode)(ZCSR_KEY_KP_0 + (vk - 0x60u));

    switch (vk) {
    case 0x1Bu: return ZCSR_KEY_ESCAPE;
    case 0x09u: return ZCSR_KEY_TAB;
    case 0x0Du: return ZCSR_KEY_ENTER;
    case 0x20u: return ZCSR_KEY_SPACE;
    case 0x08u: return ZCSR_KEY_BACKSPACE;
    case 0x2Eu: return ZCSR_KEY_DELETE;
    case 0x2Du: return ZCSR_KEY_INSERT;
    case 0x24u: return ZCSR_KEY_HOME;
    case 0x23u: return ZCSR_KEY_END;
    case 0x21u: return ZCSR_KEY_PAGE_UP;
    case 0x22u: return ZCSR_KEY_PAGE_DOWN;
    case 0x25u: return ZCSR_KEY_ARROW_LEFT;
    case 0x27u: return ZCSR_KEY_ARROW_RIGHT;
    case 0x26u: return ZCSR_KEY_ARROW_UP;
    case 0x28u: return ZCSR_KEY_ARROW_DOWN;
    case 0x10u: return ZCSR_KEY_SHIFT;
    case 0x11u: return ZCSR_KEY_CTRL;
    case 0x12u: return ZCSR_KEY_ALT;
    case 0x5Bu: return ZCSR_KEY_GUI;
    case 0x5Cu: return ZCSR_KEY_GUI;
    case 0x14u: return ZCSR_KEY_CAPS_LOCK;
    case 0x90u: return ZCSR_KEY_NUM_LOCK;
    case 0x91u: return ZCSR_KEY_SCROLL_LOCK;
    case 0x2Cu: return ZCSR_KEY_PRINT_SCREEN;
    case 0x13u: return ZCSR_KEY_PAUSE;
    case 0x5Du: return ZCSR_KEY_MENU;
    case 0xC0u: return ZCSR_KEY_GRAVE;
    case 0xBDu: return ZCSR_KEY_MINUS;
    case 0xBBu: return ZCSR_KEY_EQUALS;
    case 0xDBu: return ZCSR_KEY_LEFT_BRACKET;
    case 0xDDu: return ZCSR_KEY_RIGHT_BRACKET;
    case 0xDCu: return ZCSR_KEY_BACKSLASH;
    case 0xBAu: return ZCSR_KEY_SEMICOLON;
    case 0xDEu: return ZCSR_KEY_APOSTROPHE;
    case 0xBCu: return ZCSR_KEY_COMMA;
    case 0xBEu: return ZCSR_KEY_PERIOD;
    case 0xBFu: return ZCSR_KEY_SLASH;
    case 0x6Eu: return ZCSR_KEY_KP_DECIMAL;
    case 0x6Bu: return ZCSR_KEY_KP_PLUS;
    case 0x6Du: return ZCSR_KEY_KP_MINUS;
    case 0x6Au: return ZCSR_KEY_KP_MULTIPLY;
    case 0x6Fu: return ZCSR_KEY_KP_DIVIDE;
    default:    return ZCSR_KEY_NONE;
    }
}

static zcsr_keycode from_android(uint32_t code) {
    /* Android keycodes: A-Z = 29..54, 0-9 = 7..16, F1-F12 = 131..142. */
    if (code >= 29u && code <= 54u) return (zcsr_keycode)(ZCSR_KEY_A + (code - 29u));
    if (code >= 7u && code <= 16u) return (zcsr_keycode)(ZCSR_KEY_0 + (code - 7u));
    if (code >= 131u && code <= 142u) return (zcsr_keycode)(ZCSR_KEY_F1 + (code - 131u));
    if (code >= 144u && code <= 153u) return (zcsr_keycode)(ZCSR_KEY_KP_0 + (code - 144u));

    switch (code) {
    case 19u: return ZCSR_KEY_ARROW_UP;
    case 20u: return ZCSR_KEY_ARROW_DOWN;
    case 21u: return ZCSR_KEY_ARROW_LEFT;
    case 22u: return ZCSR_KEY_ARROW_RIGHT;
    case 55u: return ZCSR_KEY_COMMA;
    case 56u: return ZCSR_KEY_PERIOD;
    case 57u: return ZCSR_KEY_ALT;
    case 58u: return ZCSR_KEY_ALT;
    case 59u: return ZCSR_KEY_SHIFT;
    case 60u: return ZCSR_KEY_SHIFT;
    case 61u: return ZCSR_KEY_TAB;
    case 62u: return ZCSR_KEY_SPACE;
    case 66u: return ZCSR_KEY_ENTER;
    case 67u: return ZCSR_KEY_BACKSPACE;
    case 68u: return ZCSR_KEY_GRAVE;
    case 69u: return ZCSR_KEY_MINUS;
    case 70u: return ZCSR_KEY_EQUALS;
    case 71u: return ZCSR_KEY_LEFT_BRACKET;
    case 72u: return ZCSR_KEY_RIGHT_BRACKET;
    case 73u: return ZCSR_KEY_BACKSLASH;
    case 74u: return ZCSR_KEY_SEMICOLON;
    case 75u: return ZCSR_KEY_APOSTROPHE;
    case 76u: return ZCSR_KEY_SLASH;
    case 78u: return ZCSR_KEY_NUM_LOCK;
    case 82u: return ZCSR_KEY_MENU;
    case 92u: return ZCSR_KEY_PAGE_UP;
    case 93u: return ZCSR_KEY_PAGE_DOWN;
    case 111u: return ZCSR_KEY_ESCAPE;
    case 112u: return ZCSR_KEY_DELETE;
    case 113u: return ZCSR_KEY_CTRL;
    case 114u: return ZCSR_KEY_CTRL;
    case 115u: return ZCSR_KEY_CAPS_LOCK;
    case 116u: return ZCSR_KEY_SCROLL_LOCK;
    case 117u: return ZCSR_KEY_GUI;
    case 118u: return ZCSR_KEY_GUI;
    case 120u: return ZCSR_KEY_PRINT_SCREEN;
    case 121u: return ZCSR_KEY_PAUSE;
    case 122u: return ZCSR_KEY_HOME;
    case 123u: return ZCSR_KEY_END;
    case 124u: return ZCSR_KEY_INSERT;
    case 154u: return ZCSR_KEY_KP_DIVIDE;
    case 155u: return ZCSR_KEY_KP_MULTIPLY;
    case 156u: return ZCSR_KEY_KP_MINUS;
    case 157u: return ZCSR_KEY_KP_PLUS;
    case 158u: return ZCSR_KEY_KP_DECIMAL;
    case 160u: return ZCSR_KEY_KP_ENTER;
    default:   return ZCSR_KEY_NONE;
    }
}

static zcsr_keycode from_cocoa(uint32_t code) {
    switch (code) {
    case 0u:   return ZCSR_KEY_A;
    case 1u:   return ZCSR_KEY_S;
    case 2u:   return ZCSR_KEY_D;
    case 3u:   return ZCSR_KEY_F;
    case 4u:   return ZCSR_KEY_H;
    case 5u:   return ZCSR_KEY_G;
    case 6u:   return ZCSR_KEY_Z;
    case 7u:   return ZCSR_KEY_X;
    case 8u:   return ZCSR_KEY_C;
    case 9u:   return ZCSR_KEY_V;
    case 11u:  return ZCSR_KEY_B;
    case 12u:  return ZCSR_KEY_Q;
    case 13u:  return ZCSR_KEY_W;
    case 14u:  return ZCSR_KEY_E;
    case 15u:  return ZCSR_KEY_R;
    case 16u:  return ZCSR_KEY_Y;
    case 17u:  return ZCSR_KEY_T;
    case 18u:  return ZCSR_KEY_1;
    case 19u:  return ZCSR_KEY_2;
    case 20u:  return ZCSR_KEY_3;
    case 21u:  return ZCSR_KEY_4;
    case 22u:  return ZCSR_KEY_6;
    case 23u:  return ZCSR_KEY_5;
    case 24u:  return ZCSR_KEY_EQUALS;
    case 25u:  return ZCSR_KEY_9;
    case 26u:  return ZCSR_KEY_7;
    case 27u:  return ZCSR_KEY_MINUS;
    case 28u:  return ZCSR_KEY_8;
    case 29u:  return ZCSR_KEY_0;
    case 30u:  return ZCSR_KEY_RIGHT_BRACKET;
    case 31u:  return ZCSR_KEY_O;
    case 32u:  return ZCSR_KEY_U;
    case 33u:  return ZCSR_KEY_LEFT_BRACKET;
    case 34u:  return ZCSR_KEY_I;
    case 35u:  return ZCSR_KEY_P;
    case 36u:  return ZCSR_KEY_ENTER;
    case 37u:  return ZCSR_KEY_L;
    case 38u:  return ZCSR_KEY_J;
    case 39u:  return ZCSR_KEY_APOSTROPHE;
    case 40u:  return ZCSR_KEY_K;
    case 41u:  return ZCSR_KEY_SEMICOLON;
    case 42u:  return ZCSR_KEY_BACKSLASH;
    case 43u:  return ZCSR_KEY_COMMA;
    case 44u:  return ZCSR_KEY_SLASH;
    case 45u:  return ZCSR_KEY_N;
    case 46u:  return ZCSR_KEY_M;
    case 47u:  return ZCSR_KEY_PERIOD;
    case 48u:  return ZCSR_KEY_TAB;
    case 49u:  return ZCSR_KEY_SPACE;
    case 50u:  return ZCSR_KEY_GRAVE;
    case 51u:  return ZCSR_KEY_BACKSPACE;
    case 53u:  return ZCSR_KEY_ESCAPE;
    case 54u:  return ZCSR_KEY_GUI;
    case 55u:  return ZCSR_KEY_GUI;
    case 56u:  return ZCSR_KEY_SHIFT;
    case 57u:  return ZCSR_KEY_CAPS_LOCK;
    case 58u:  return ZCSR_KEY_ALT;
    case 59u:  return ZCSR_KEY_CTRL;
    case 60u:  return ZCSR_KEY_SHIFT;
    case 61u:  return ZCSR_KEY_ALT;
    case 62u:  return ZCSR_KEY_CTRL;
    case 65u:  return ZCSR_KEY_KP_DECIMAL;
    case 67u:  return ZCSR_KEY_KP_MULTIPLY;
    case 69u:  return ZCSR_KEY_KP_PLUS;
    case 75u:  return ZCSR_KEY_KP_DIVIDE;
    case 76u:  return ZCSR_KEY_KP_ENTER;
    case 78u:  return ZCSR_KEY_KP_MINUS;
    case 82u:  return ZCSR_KEY_KP_0;
    case 83u:  return ZCSR_KEY_KP_1;
    case 84u:  return ZCSR_KEY_KP_2;
    case 85u:  return ZCSR_KEY_KP_3;
    case 86u:  return ZCSR_KEY_KP_4;
    case 87u:  return ZCSR_KEY_KP_5;
    case 88u:  return ZCSR_KEY_KP_6;
    case 89u:  return ZCSR_KEY_KP_7;
    case 91u:  return ZCSR_KEY_KP_8;
    case 92u:  return ZCSR_KEY_KP_9;
    case 96u:  return ZCSR_KEY_F5;
    case 97u:  return ZCSR_KEY_F6;
    case 98u:  return ZCSR_KEY_F7;
    case 99u:  return ZCSR_KEY_F3;
    case 100u: return ZCSR_KEY_F8;
    case 101u: return ZCSR_KEY_F9;
    case 103u: return ZCSR_KEY_F11;
    case 109u: return ZCSR_KEY_F10;
    case 111u: return ZCSR_KEY_F12;
    case 114u: return ZCSR_KEY_INSERT;
    case 115u: return ZCSR_KEY_HOME;
    case 116u: return ZCSR_KEY_PAGE_UP;
    case 117u: return ZCSR_KEY_DELETE;
    case 118u: return ZCSR_KEY_F4;
    case 119u: return ZCSR_KEY_END;
    case 120u: return ZCSR_KEY_F2;
    case 121u: return ZCSR_KEY_PAGE_DOWN;
    case 122u: return ZCSR_KEY_F1;
    case 123u: return ZCSR_KEY_ARROW_LEFT;
    case 124u: return ZCSR_KEY_ARROW_RIGHT;
    case 125u: return ZCSR_KEY_ARROW_DOWN;
    case 126u: return ZCSR_KEY_ARROW_UP;
    default:   return ZCSR_KEY_NONE;
    }
}

zcsr_keycode zcsr_keycode_from_native(zcsr_key_domain domain, uint32_t native_code) {
    switch (domain) {
    case ZCSR_KEY_DOMAIN_WIN32_VK:        return from_win32(native_code);
    case ZCSR_KEY_DOMAIN_X11_KEYCODE:     return native_code >= 8u
                                                   ? from_evdev(native_code - 8u)
                                                   : ZCSR_KEY_NONE;
    case ZCSR_KEY_DOMAIN_EVDEV:           return from_evdev(native_code);
    case ZCSR_KEY_DOMAIN_ANDROID_KEYCODE: return from_android(native_code);
    case ZCSR_KEY_DOMAIN_COCOA_KEYCODE:   return from_cocoa(native_code);
    default:                              return ZCSR_KEY_NONE;
    }
}
