#ifndef ZCSR_UI_DRAW_H
#define ZCSR_UI_DRAW_H
/* Backend-neutral draw protocol. The core emits abstract draw commands and
 * never touches a graphics API; a host backend (e.g. ui_gl_backend) turns them
 * into real calls. Label keys are resolved to concrete text here, so the backend
 * only renders strings. No heap: the command list is caller-owned.
 * Implemented in modules/ui. */
#include <stddef.h>            /* size_t */
#include "zcsr/ui_panel.h"
#include "zcsr/ui_i18n.h"
#include "zcsr/ui_node.h"       /* zcsr_ui_key / zcsr_ui_rect / zcsr_ui_widget_type */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZCSR_UI_DRAW_WIDGET = 1,      /* node body, with interaction state */
    ZCSR_UI_DRAW_LABEL,           /* resolved node label text */
    ZCSR_UI_DRAW_POPUP,           /* open dropdown popup background */
    ZCSR_UI_DRAW_POPUP_OPTION     /* one resolved option row in the popup */
} zcsr_ui_draw_kind;

typedef struct {
    zcsr_ui_draw_kind   kind;
    zcsr_ui_id          node_id;
    zcsr_ui_rect        rect;
    zcsr_ui_widget_type widget_type;

    zcsr_ui_key         resource_key;
    zcsr_ui_value       value;

    /* Sub-rectangle of the resource texture to sample (IMAGE only), normalized 0..1.
     * Zero-initialized means "whole image" and is what every non-image caller gets. */
    zcsr_ui_rect        uv_rect;

    /* Slider bounds, copied from the node so the backend can place the knob. */
    float               minimum;
    float               maximum;

    /* Concrete text resolved from an i18n key; NULL when the node has no text. */
    const char*         text;
    size_t              text_length;

    bool enabled;
    bool focused;
    bool hovered;
    bool pressed;
    bool selected;
    bool dirty;
    bool opened;
} zcsr_ui_draw_command;

typedef struct {
    zcsr_ui_draw_command* items;
    size_t                capacity;
    size_t                count;
} zcsr_ui_draw_list;

/* 注意：这里曾经声明过一个 zcsr_ui_build_draw_list(ctx, active, fallback, out)，
 * 但它从未被实现过，也没有任何调用者 —— 是一次"有声明无实现"的假 API，而且与
 * ui_draw_v2.h 里的 zcsr_ui_build_draw_list(ZCSR_UI*) 同名不同签名（C 不支持重载，
 * 两个头一起 include 会触发 C4031/C4028）。已删除。
 * 当前活着的入口是 ui_draw_v2.h 的 zcsr_ui_build_draw_list(ZCSR_UI*)，实现在 modules/ui/ui.c。
 * zcsr_ui_draw_list 结构本身保留：ui_gl_backend 的 v1 入口仍引用它（该入口当前无调用者）。 */

#ifdef __cplusplus
}
#endif

#endif /* ZCSR_UI_DRAW_H */
