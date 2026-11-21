#ifndef ZCSR_INPUT_H
#define ZCSR_INPUT_H
/* keyboard input (key down / key up). Implementer: modules/input.
 * Events are buffered in a fixed ring (no heap). Pumped from the platform window. */
#include "platform.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum { ZCSR_KEY_UP = 0, ZCSR_KEY_DOWN = 1 } zcsr_key_action;

/* Platform-independent physical-key codes. Backends translate their native
 * scancode / virtual-key / keycode into these values at the event boundary.
 * `key` is never a character; text is delivered by `text` (Win32 WM_CHAR) or
 * `zcsr_input_ext_take_text` (X11/Wayland/Android/Cocoa).
 *
 * Naming note: the legacy action enum already owns ZCSR_KEY_UP/ZCSR_KEY_DOWN,
 * so arrow keys are named ZCSR_KEY_ARROW_* to avoid a C name collision. */
enum {
    ZCSR_KEY_NONE = 0,
    ZCSR_KEY_ESCAPE = 1,
    ZCSR_KEY_TAB = 2,
    ZCSR_KEY_ENTER = 3,
    ZCSR_KEY_SPACE = 4,
    ZCSR_KEY_BACKSPACE = 5,
    ZCSR_KEY_DELETE = 6,
    ZCSR_KEY_INSERT = 7,
    ZCSR_KEY_HOME = 8,
    ZCSR_KEY_END = 9,
    ZCSR_KEY_PAGE_UP = 10,
    ZCSR_KEY_PAGE_DOWN = 11,
    ZCSR_KEY_ARROW_LEFT = 12,
    ZCSR_KEY_ARROW_RIGHT = 13,
    ZCSR_KEY_ARROW_UP = 14,
    ZCSR_KEY_ARROW_DOWN = 15,
    ZCSR_KEY_SHIFT = 16,
    ZCSR_KEY_CTRL = 17,
    ZCSR_KEY_ALT = 18,
    ZCSR_KEY_GUI = 19,
    ZCSR_KEY_CAPS_LOCK = 20,
    ZCSR_KEY_NUM_LOCK = 21,
    ZCSR_KEY_SCROLL_LOCK = 22,
    ZCSR_KEY_PRINT_SCREEN = 23,
    ZCSR_KEY_PAUSE = 24,
    ZCSR_KEY_MENU = 25,
    ZCSR_KEY_F1 = 26, ZCSR_KEY_F2, ZCSR_KEY_F3, ZCSR_KEY_F4,
    ZCSR_KEY_F5, ZCSR_KEY_F6, ZCSR_KEY_F7, ZCSR_KEY_F8,
    ZCSR_KEY_F9, ZCSR_KEY_F10, ZCSR_KEY_F11, ZCSR_KEY_F12,
    ZCSR_KEY_A = 38, ZCSR_KEY_B, ZCSR_KEY_C, ZCSR_KEY_D,
    ZCSR_KEY_E, ZCSR_KEY_F, ZCSR_KEY_G, ZCSR_KEY_H,
    ZCSR_KEY_I, ZCSR_KEY_J, ZCSR_KEY_K, ZCSR_KEY_L,
    ZCSR_KEY_M, ZCSR_KEY_N, ZCSR_KEY_O, ZCSR_KEY_P,
    ZCSR_KEY_Q, ZCSR_KEY_R, ZCSR_KEY_S, ZCSR_KEY_T,
    ZCSR_KEY_U, ZCSR_KEY_V, ZCSR_KEY_W, ZCSR_KEY_X,
    ZCSR_KEY_Y, ZCSR_KEY_Z,
    ZCSR_KEY_0 = 64, ZCSR_KEY_1, ZCSR_KEY_2, ZCSR_KEY_3,
    ZCSR_KEY_4, ZCSR_KEY_5, ZCSR_KEY_6, ZCSR_KEY_7,
    ZCSR_KEY_8, ZCSR_KEY_9,
    ZCSR_KEY_GRAVE = 74,
    ZCSR_KEY_MINUS = 75,
    ZCSR_KEY_EQUALS = 76,
    ZCSR_KEY_LEFT_BRACKET = 77,
    ZCSR_KEY_RIGHT_BRACKET = 78,
    ZCSR_KEY_BACKSLASH = 79,
    ZCSR_KEY_SEMICOLON = 80,
    ZCSR_KEY_APOSTROPHE = 81,
    ZCSR_KEY_COMMA = 82,
    ZCSR_KEY_PERIOD = 83,
    ZCSR_KEY_SLASH = 84,
    ZCSR_KEY_KP_0 = 85, ZCSR_KEY_KP_1, ZCSR_KEY_KP_2, ZCSR_KEY_KP_3,
    ZCSR_KEY_KP_4, ZCSR_KEY_KP_5, ZCSR_KEY_KP_6, ZCSR_KEY_KP_7,
    ZCSR_KEY_KP_8, ZCSR_KEY_KP_9,
    ZCSR_KEY_KP_DECIMAL = 95,
    ZCSR_KEY_KP_ENTER = 96,
    ZCSR_KEY_KP_PLUS = 97,
    ZCSR_KEY_KP_MINUS = 98,
    ZCSR_KEY_KP_MULTIPLY = 99,
    ZCSR_KEY_KP_DIVIDE = 100,
    ZCSR_KEY_COUNT = 101
};

typedef uint16_t zcsr_keycode; /* one of ZCSR_KEY_* above */

typedef struct {
    zcsr_keycode    key;    /* ZCSR_KEY_* for physical keys; ZCSR_KEY_NONE for text */
    zcsr_key_action action;
    bool            text;   /* Win32 WM_CHAR/IME character, not a physical key;
                               Linux/Android/macOS text is delivered by
                               zcsr_input_ext_take_text instead */
} zcsr_key_event;

typedef struct zcsr_input zcsr_input; /* opaque */

zcsr_input* zcsr_input_init(void* buffer, size_t bytes);
/* Drain OS key messages from the window into the queue (call once per frame). */
void        zcsr_input_pump(zcsr_input*, zcsr_surface* window);
/* Dequeue the next key event; false when the queue is empty. */
bool        zcsr_input_poll(zcsr_input*, zcsr_key_event* out);
/* Current held state of a key (down/up). */
bool        zcsr_input_is_down(const zcsr_input*, zcsr_keycode);

/* Queue a synthetic physical-key event through the same event ring and key_down
 * state as platform events. This is a test/tool entry point: platform backends
 * must translate their native key at the event boundary and call the pump, not
 * this function. Returns false for an invalid key or a missing input. */
bool        zcsr_input_inject_key(zcsr_input*, zcsr_keycode key, zcsr_key_action action);

#endif /* ZCSR_INPUT_H */
