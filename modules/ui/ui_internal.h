#ifndef ZCSR_UI_INTERNAL_H
#define ZCSR_UI_INTERNAL_H

#include "zcsr/ui.h"
#include "zcsr/ui_event.h"
#include "zcsr/ui_input.h"
#include "zcsr/ui_draw_v2.h"
#include "zcsr/ui_button.h"
#include <stdlib.h>
#include <string.h>

/* Internal node representation. */
typedef struct ZCSR_UI_Node {
    ZCSR_NodeId       id;
    ZCSR_ComponentId  component;
    uint32_t          flags;
    ZCSR_Rect         local_rect;
    int32_t           z_order;
    ZCSR_NodeId       parent;
    ZCSR_NodeId       first_child;
    ZCSR_NodeId       next_sibling;

    /* Per-component state storage (type-specific). */
    bool              bool_val;
    float             float_val;
    float             float_min, float_max, float_step;
    int32_t           int_val;
    float             scroll_x, scroll_y;
    float             content_w, content_h;
    char*             text_buf;
    uint32_t          text_cap;
    uint32_t          caret_byte;

    /* Image component state. resource_key 属于宿主的纹理表（引擎不认识图片内容）：
     * 0 = 未设置（draw list 会画"缺图"样式，不再假装有图）。 */
    uint32_t          resource_key;
    float             uv_x, uv_y, uv_w, uv_h;   /* 归一化 0..1；未设置时按整图处理 */
    int32_t           fit_mode;                 /* 0=stretch 1=contain 2=cover */

    /* Style: editor-adjustable colors. */
    ZCSR_Color        bg_color;
    ZCSR_Color        text_color;
    ZCSR_Color        border_color;
    bool              bg_override;      /* true = use bg_color, false = default */
    bool              text_override;
    bool              border_override;

    /* Interaction state. */
    bool              hovered;
    bool              pressed;
    bool              focused;
    bool              enabled;
    bool              visible;
    bool              popup_open;

    /* Dropdown option storage (bounded, no heap). */
#define ZCSR_DROPDOWN_MAX_ITEMS 16
#define ZCSR_DROPDOWN_MAX_TEXT  32
    char              items[ZCSR_DROPDOWN_MAX_ITEMS][ZCSR_DROPDOWN_MAX_TEXT];
    uint32_t          item_count;
    float             popup_max_h;
} ZCSR_UI_Node;

struct ZCSR_UI {
    ZCSR_UI_Node* nodes;
    uint32_t      node_count;
    uint32_t      node_capacity;
    ZCSR_NodeId   next_id;

    ZCSR_UIEvent* events;
    uint32_t      event_cap;
    uint32_t      event_head;
    uint32_t      event_count;
    uint64_t      event_sequence;

    ZCSR_DrawCommand* draw_cmds;
    uint32_t          draw_cap;
    uint32_t          draw_count;

    float viewport_w, viewport_h;
    ZCSR_NodeId focus_id;
    ZCSR_NodeId pressed_id;
    ZCSR_NodeId scroll_capture_id;
    float       scroll_capture_start_y;
    float       scroll_capture_start_offset;
    uint32_t    captured_pointer;
};

ZCSR_UI_Node* ui_find_node(ZCSR_UI* ui, ZCSR_NodeId id);
const ZCSR_UI_Node* ui_find_node_c(const ZCSR_UI* ui, ZCSR_NodeId id);
void ui_emit_event(ZCSR_UI* ui, ZCSR_EventTypeEnum type, ZCSR_NodeId src,
                   ZCSR_ComponentId comp);

#endif
