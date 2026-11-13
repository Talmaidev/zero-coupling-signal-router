#ifndef ZCSR_UI_INPUT_H
#define ZCSR_UI_INPUT_H
/* ZCSR UI Runtime — unified input protocol.
 * Platform adapters (Win32, SDL, Wayland) translate native events into this
 * struct; primitives never touch platform APIs directly. */
#include "zcsr/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ZCSR_InputType {
    ZCSR_INPUT_POINTER_MOVE,
    ZCSR_INPUT_POINTER_DOWN,
    ZCSR_INPUT_POINTER_UP,
    ZCSR_INPUT_POINTER_WHEEL,
    ZCSR_INPUT_KEY_DOWN,
    ZCSR_INPUT_KEY_UP,
    ZCSR_INPUT_TEXT
} ZCSR_InputType;

typedef enum ZCSR_PointerButton {
    ZCSR_POINTER_NONE = 0,
    ZCSR_POINTER_LEFT,
    ZCSR_POINTER_RIGHT,
    ZCSR_POINTER_MIDDLE
} ZCSR_PointerButton;

typedef enum ZCSR_Key {
    ZCSR_KEY_UNKNOWN = 0,
    ZCSR_KEY_TAB,
    ZCSR_KEY_ENTER,
    ZCSR_KEY_ESCAPE,
    ZCSR_KEY_SPACE,
    ZCSR_KEY_LEFT,
    ZCSR_KEY_RIGHT,
    ZCSR_KEY_UP,
    ZCSR_KEY_DOWN,
    ZCSR_KEY_HOME,
    ZCSR_KEY_END,
    ZCSR_KEY_BACKSPACE,
    ZCSR_KEY_DELETE,
    ZCSR_KEY_PAGE_UP,
    ZCSR_KEY_PAGE_DOWN
} ZCSR_Key;

typedef struct ZCSR_Input {
    ZCSR_InputType      type;
    ZCSR_Vec2           pointer_position;
    ZCSR_PointerButton  pointer_button;
    float               wheel_x, wheel_y;
    ZCSR_Key            key;
    uint32_t            modifiers;
    const char*         text_utf8;
    uint32_t            text_bytes;
    uint32_t            pointer_id;
} ZCSR_Input;

typedef struct ZCSR_UI ZCSR_UI;

ZCSR_Result zcsr_ui_process_input(ZCSR_UI* ui, const ZCSR_Input* input);
ZCSR_Result zcsr_ui_release_pointer(ZCSR_UI* ui, uint32_t pointer_id);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_INPUT_H */
