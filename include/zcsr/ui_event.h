#ifndef ZCSR_UI_EVENT_H
#define ZCSR_UI_EVENT_H
/* ZCSR UI Runtime — event types and payload. */
#include "zcsr/ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ZCSR_EventTypeEnum {
    ZCSR_EVENT_NONE = 0,
    ZCSR_EVENT_BUTTON_CLICKED,
    ZCSR_EVENT_TOGGLE_CHANGED,
    ZCSR_EVENT_SLIDER_CHANGED,
    ZCSR_EVENT_DROPDOWN_CHANGED,
    ZCSR_EVENT_DROPDOWN_OPENED,
    ZCSR_EVENT_DROPDOWN_CLOSED,
    ZCSR_EVENT_TEXT_CHANGED,
    ZCSR_EVENT_TEXT_SUBMITTED,
    ZCSR_EVENT_TEXT_CANCELLED,
    ZCSR_EVENT_SCROLL_CHANGED,
    ZCSR_EVENT_PANEL_ACTIVATED
} ZCSR_EventTypeEnum;

typedef struct ZCSR_EventPayload {
    union {
        struct { bool value; } toggle;
        struct { float value; float normalized; } slider;
        struct { int32_t selected_index; uint32_t item_count; } dropdown;
        struct {
            const char* utf8;
            uint32_t byte_length;
            uint32_t cursor_byte;
            uint32_t selection_start_byte;
            uint32_t selection_end_byte;
        } text;
        struct {
            float offset_x, offset_y;
            float content_width, content_height;
        } scroll;
    } data;
} ZCSR_EventPayload;

typedef struct ZCSR_UIEvent {
    ZCSR_EventTypeEnum type;
    ZCSR_NodeId        source_node;
    ZCSR_ComponentId   source_component;
    uint64_t           sequence;
    ZCSR_EventPayload  payload;
} ZCSR_UIEvent;

/* Event queue API is defined in ui.h / ui.c. */
typedef struct ZCSR_UI ZCSR_UI;

ZCSR_Result zcsr_ui_poll_event(ZCSR_UI* ui, ZCSR_UIEvent* out_event);
ZCSR_Result zcsr_ui_peek_event(const ZCSR_UI* ui, ZCSR_UIEvent* out_event);
ZCSR_Result zcsr_ui_clear_events(ZCSR_UI* ui);

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_EVENT_H */
