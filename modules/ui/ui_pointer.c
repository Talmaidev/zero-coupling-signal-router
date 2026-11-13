/* Flat-node hit testing + single-pointer capture. Pure CPU, caller-owned state. */
#include "zcsr/ui_node.h"
#include "zcsr/ui_panel.h"

zcsr_ui_id zcsr_ui_hit_test(const zcsr_ui_node* nodes, size_t count, float x, float y)
{
    size_t i;

    if (!nodes || count == 0u)
        return ZCSR_UI_ID_NONE;

    /* Later entries are the topmost layer; scan backwards so the first match wins. */
    for (i = count; i > 0u; --i) {
        const zcsr_ui_node* node = &nodes[i - 1u];
        if (!node->desc.visible || !node->desc.enabled)
            continue;
        if (!zcsr_ui_node_is_interactive(node->desc.type))
            continue;
        if (zcsr_ui_rect_contains(node->desc.rect, x, y))
            return node->desc.id;
    }
    return ZCSR_UI_ID_NONE;
}

zcsr_ui_id zcsr_ui_pointer_down(zcsr_ui_context* ctx, const zcsr_ui_node* nodes,
                                size_t count, float x, float y)
{
    if (!ctx)
        return ZCSR_UI_ID_NONE;
    ctx->pressed_id = zcsr_ui_hit_test(nodes, count, x, y);
    ctx->hot_id = ctx->pressed_id;
    return ctx->pressed_id;
}

zcsr_ui_id zcsr_ui_pointer_up(zcsr_ui_context* ctx, const zcsr_ui_node* nodes,
                              size_t count, float x, float y)
{
    zcsr_ui_id captured;
    zcsr_ui_id hit;

    if (!ctx)
        return ZCSR_UI_ID_NONE;
    captured = ctx->pressed_id;
    ctx->pressed_id = ZCSR_UI_ID_NONE;
    hit = zcsr_ui_hit_test(nodes, count, x, y);
    ctx->hot_id = hit;
    if (captured == ZCSR_UI_ID_NONE)
        return ZCSR_UI_ID_NONE;
    return (hit == captured) ? captured : ZCSR_UI_ID_NONE;
}

zcsr_ui_id zcsr_ui_pointer_owner(const zcsr_ui_context* ctx)
{
    return ctx ? ctx->pressed_id : ZCSR_UI_ID_NONE;
}
