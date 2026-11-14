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

/* Pointer capture over the same flat node array used by zcsr_ui_hit_test.
 * down: stores the topmost hit id as pressed_id/hot_id and returns it.
 * up: returns the captured id only when released over the same node; always clears capture.
 * owner: current captured id (ZCSR_UI_ID_NONE when no button is down). */
zcsr_ui_id zcsr_ui_pointer_down(zcsr_ui_context* ctx, const zcsr_ui_node* nodes,
                                size_t count, float x, float y);
zcsr_ui_id zcsr_ui_pointer_up(zcsr_ui_context* ctx, const zcsr_ui_node* nodes,
                              size_t count, float x, float y);
zcsr_ui_id zcsr_ui_pointer_owner(const zcsr_ui_context* ctx);

/* 面板运行时（旧链）的函数声明已全部移除：它们从未有过实现，也没有任何调用者。
 *
 * 移除的直接原因是一个硬冲突：
 *   ui_panel.h   -> bool        zcsr_ui_poll_event(zcsr_ui_context*, zcsr_ui_event*)
 *   ui_event.h   -> ZCSR_Result zcsr_ui_poll_event(ZCSR_UI*, ZCSR_UIEvent*)
 * 同名不同签名，任何同时 include 两个头的翻译单元都会报 C4031/C4028。
 * ui.c 实现并实际使用的是 ui_event.h 那一套。
 *
 * 本文件到此只保留类型定义（zcsr_ui_status / input / event / event_queue / context），
 * 供仍依赖这些类型的头文件（如 ui_draw.h）继续编译。要面板运行时请用 ui_event.h。
 */

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_PANEL_H */
