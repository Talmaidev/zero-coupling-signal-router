#ifndef ZCSR_INPUT_EXT_H
#define ZCSR_INPUT_EXT_H
/* Engine-ext Agent 3 — mouse + gamepad input. ADDITIVE to zcsr/input.h (keyboard there is
 * unchanged). Implementer: modules/inputext.
 * Backend status: Win32 RawInput+XInput+WM_CHAR text; Linux X11 XIM/XIC text
 * and evdev gamepads; Linux Wayland xkbcommon text and evdev gamepads;
 * Android touch+joystick and JNI KeyEvent physical-keyboard text; macOS
 * Cocoa NSEvent mouse/physical-key text and GameController gamepads.
 * Soft-keyboard IME is not implemented on Android, Wayland or macOS.
 * No runtime heap: state lives in a caller buffer. */
#include "platform.h"  /* zcsr_surface */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---- Mouse ---- */
typedef enum { ZCSR_MOUSE_LEFT = 1u, ZCSR_MOUSE_RIGHT = 2u, ZCSR_MOUSE_MIDDLE = 4u } zcsr_mouse_button;

/* Platform-independent modifier bits captured at a mouse press. */
typedef enum {
    ZCSR_MOD_CTRL  = 1u << 0,
    ZCSR_MOD_SHIFT = 1u << 1,
    ZCSR_MOD_ALT   = 1u << 2,
    ZCSR_MOD_GUI   = 1u << 3
} zcsr_modifier;

typedef struct {
    int     x, y;     /* position in window (top-left origin) */
    int     dx, dy;   /* delta since last pump; first sample is 0 */
    int     wheel;    /* wheel ticks accumulated since last pump */
    uint8_t buttons;  /* OR of zcsr_mouse_button currently held */
    uint8_t pressed;  /* zcsr_mouse_button bits that went up->down since last pump */
    uint8_t released; /* zcsr_mouse_button bits that went down->up since last pump */
    int     down_x, down_y; /* left-button press position; valid only while have_down */
    bool    have_down;      /* a left-button press is currently held; cleared on release or focus loss */
    uint8_t down_mods;      /* ZCSR_MOD_* captured at that press; never Win32 MK_* */
} zcsr_mouse_state;

/* Derive one-frame pressed/released edges from the current held mask and the
 * previous frame's held mask. Pure function; backends call it once per pump.
 * `previous_buttons` is the held mask returned by the previous pump. */
void zcsr_mouse_state_update_edges(zcsr_mouse_state* state, uint8_t previous_buttons);

/* Merge queued backend event edges into this frame's pressed/released.
 * Event-driven backends can deliver down+up between two pumps; the held mask
 * is then already 0 and derived edges would be lost. Both masks are clipped to
 * ZCSR_MOUSE_LEFT/RIGHT/MIDDLE. Pure function. */
void zcsr_mouse_state_merge_event_edges(zcsr_mouse_state* state,
                                        uint8_t pressed, uint8_t released);

/* ---- Text input capability ----
 * The engine reports what the active build really provides, instead of
 * claiming "IME" from a Unicode keycode. PHYSICAL = key events produce UTF-8.
 * IME_COMMIT = an OS input method may commit composed text. IME_PREEDIT =
 * preedit/candidate UI is owned by the engine; this is not implemented on any
 * current backend, so the bit is defined but never set. */
enum {
    ZCSR_TEXT_INPUT_PHYSICAL     = 1u << 0,
    ZCSR_TEXT_INPUT_IME_COMMIT   = 1u << 1,
    ZCSR_TEXT_INPUT_IME_PREEDIT  = 1u << 2
};
unsigned zcsr_input_ext_text_input_capabilities(void);

/* Full soft-keyboard/IME path (show keyboard, preedit, candidate window).
 * False on Android/Wayland/macOS/X11/Win32 today; callers must not assume it. */
bool zcsr_input_ext_soft_keyboard_available(void);

/* ---- Gamepad (up to 2, Xbox layout) ---- */
enum { ZCSR_PAD_MAX = 2 };
typedef enum {
    ZCSR_PAD_A, ZCSR_PAD_B, ZCSR_PAD_X, ZCSR_PAD_Y,
    ZCSR_PAD_LB, ZCSR_PAD_RB, ZCSR_PAD_BACK, ZCSR_PAD_START,
    ZCSR_PAD_LSTICK, ZCSR_PAD_RSTICK,
    ZCSR_PAD_DUP, ZCSR_PAD_DDOWN, ZCSR_PAD_DLEFT, ZCSR_PAD_DRIGHT,
    ZCSR_PAD_BUTTON_COUNT
} zcsr_pad_button;
typedef struct {
    bool     connected;
    uint16_t buttons;       /* bitmask: (1u << zcsr_pad_button) */
    float    lx, ly, rx, ry;/* sticks, -1..1 */
    float    lt, rt;        /* triggers, 0..1 */
} zcsr_pad_state;

typedef struct zcsr_input_ext zcsr_input_ext; /* opaque; mouse + gamepad state */

zcsr_input_ext*  zcsr_input_ext_init(void* buffer, size_t bytes);     /* NULL if buffer too small */
void             zcsr_input_ext_shutdown(zcsr_input_ext*);
void             zcsr_input_ext_pump(zcsr_input_ext*, zcsr_surface*); /* call once per frame */
zcsr_mouse_state zcsr_input_ext_mouse(const zcsr_input_ext*);
/* Drains complete UTF-8 text produced by the backend since the last call.
 * Producers: Win32 WM_CHAR/WM_IME_CHAR; X11 XIM/XIC (Xutf8LookupString);
 * Wayland xkbcommon; Android KeyEvent.getUnicodeChar via JNI; Cocoa NSEvent
 * characters. Returns bytes copied (0 when no text is pending). The queue is
 * byte-bounded; excess text is dropped rather than blocking the window. */
size_t           zcsr_input_ext_take_text(zcsr_input_ext*, char* out, size_t capacity);
/* Win32 XInput, Linux evdev, Android NativeActivity joystick, macOS
 * GameController. Returns a disconnected zero state for an absent slot. */
zcsr_pad_state   zcsr_input_ext_pad(const zcsr_input_ext*, int index);/* index 0..ZCSR_PAD_MAX-1 */

/* ---- Touch (single primary pointer) ---- */
typedef struct {
    float x, y;             /* window-local pixels, top-left origin */
    float dx, dy;           /* accumulated movement since last pump */
    bool active;            /* primary touch currently active */
    bool pressed;           /* a down edge occurred since last pump */
    bool released;          /* an up/cancel edge occurred since last pump */
} zcsr_touch_state;

/* Android only; other backends return a zero (inactive) state. */
zcsr_touch_state zcsr_input_ext_touch(const zcsr_input_ext*);

#endif /* ZCSR_INPUT_EXT_H */
