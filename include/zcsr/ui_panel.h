#ifndef ZCSR_UI_PANEL_H
#define ZCSR_UI_PANEL_H
/* Generic panel runtime: input routing, focus, pointer interaction, pending vs
 * committed state and the Apply confirmation protocol. The panel never calls
 * engine APIs; the assembly layer consumes events and lands real changes.
 * No heap: all storage is caller-owned. Implemented in modules/ui. */
#include "zcsr/ui_node.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZCSR_UI_OK = 0,
    ZCSR_UI_ERR_ARGUMENT,
    ZCSR_UI_ERR_RANGE,
    ZCSR_UI_ERR_CAPACITY,
    ZCSR_UI_ERR_NOT_FOUND,
    ZCSR_UI_ERR_TYPE,
    ZCSR_UI_ERR_STATE,
    ZCSR_UI_ERR_CONFLICT
} zcsr_ui_status;

/* Normalized key codes. Printable/OS keys occupy the low range; special keys
 * start at 256. The platform adapter maps its keycodes onto these. */
#define ZCSR_UI_KEY_CAPACITY 320
enum {
    ZCSR_UI_KEY_TAB = 256,
    ZCSR_UI_KEY_ENTER,
    ZCSR_UI_KEY_ESCAPE,
    ZCSR_UI_KEY_SPACE,
    ZCSR_UI_KEY_LEFT,
    ZCSR_UI_KEY_RIGHT,
    ZCSR_UI_KEY_UP,
    ZCSR_UI_KEY_DOWN,
    ZCSR_UI_KEY_HOME,
    ZCSR_UI_KEY_END
};

/* Per-frame input, filled by the host adapter from platform mouse/keyboard. */
typedef struct {
    float mouse_x;
    float mouse_y;

    bool  mouse_down;      /* button held */
    bool  mouse_pressed;   /* down edge this frame */
    bool  mouse_released;  /* up edge this frame */
    float wheel;           /* wheel ticks this frame */

    /* Edge flags: set for each normalized key pressed this frame. */
    bool  key_pressed[ZCSR_UI_KEY_CAPACITY];

    /* IME / text input: UTF-8 chars committed this frame (caller fills). */
    char  text_input[64];
    size_t text_input_len;
} zcsr_ui_input;

typedef enum {
    ZCSR_UI_EVENT_NONE = 0,
    ZCSR_UI_EVENT_ACTION,         /* button activated */
    ZCSR_UI_EVENT_VALUE_CHANGED,  /* editable scalar pending value changed */
    ZCSR_UI_EVENT_FOCUS_CHANGED,
    ZCSR_UI_EVENT_APPLY_REQUEST,
    ZCSR_UI_EVENT_APPLY_FAILED,
    ZCSR_UI_EVENT_CANCEL,
    ZCSR_UI_EVENT_RESET,
    ZCSR_UI_EVENT_TEXT_CHANGED,   /* TextInput buffer edited */
    ZCSR_UI_EVENT_TEXT_SUBMIT,    /* TextInput Enter pressed */
    ZCSR_UI_EVENT_TEXT_CANCEL,    /* TextInput Esc pressed */
    ZCSR_UI_EVENT_SCROLL_CHANGED   /* ScrollView offset changed */
} zcsr_ui_event_type;

typedef struct {
    zcsr_ui_event_type type;
    zcsr_ui_id         target_id;
    zcsr_ui_key        action_key;
    zcsr_ui_key        binding_key;
    zcsr_ui_value      value;
    uint32_t           serial;
} zcsr_ui_event;

/* Fixed ring buffer of events, caller-owned. */
typedef struct {
    zcsr_ui_event* items;
    size_t         capacity;
    size_t         head;
    size_t         count;
    uint32_t       dropped;
} zcsr_ui_event_queue;

typedef struct {
    zcsr_ui_node* nodes;
    size_t        node_count;

    zcsr_ui_event_queue events;

    zcsr_ui_id focus_id;
    zcsr_ui_id pressed_id;
    zcsr_ui_id popup_id;
    zcsr_ui_id hot_id;

    uint32_t apply_serial;
    uint32_t apply_observed_serial;
    bool     apply_pending;
    bool     apply_failed;
} zcsr_ui_context;

/* Initialize a context over a flat node array and an event storage ring.
 * Node ids must be unique and non-zero. */
zcsr_ui_status zcsr_ui_init(zcsr_ui_context* ctx,
                            zcsr_ui_node* nodes, size_t node_count,
                            zcsr_ui_event* event_storage, size_t event_capacity);

/* Advance the runtime one frame. */
void zcsr_ui_update(zcsr_ui_context* ctx, const zcsr_ui_input* input);

/* Dequeue the next event; false when the queue is empty. */
bool zcsr_ui_poll_event(zcsr_ui_context* ctx, zcsr_ui_event* out);

/* Dirty state: editable node whose pending differs from committed. */
bool zcsr_ui_node_dirty(const zcsr_ui_node* node);
bool zcsr_ui_any_dirty(const zcsr_ui_context* ctx);

/* Apply protocol. */
uint32_t zcsr_ui_request_apply(zcsr_ui_context* ctx);
bool     zcsr_ui_apply_requested(const zcsr_ui_context* ctx, uint32_t* serial);
void     zcsr_ui_confirm_apply(zcsr_ui_context* ctx, uint32_t serial, bool success);

/* Revert pending to committed; reset pending to initial. */
void zcsr_ui_cancel_all(zcsr_ui_context* ctx);
void zcsr_ui_reset_all(zcsr_ui_context* ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_PANEL_H */
