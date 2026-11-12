/* modules/ui/ui_scroll_view.c — ScrollView component. */
#include "ui_internal.h"

ZCSR_Result zcsr_scroll_view_set_content_size(ZCSR_UI* ui, ZCSR_NodeId id, float w, float h)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->content_w = w;
    nd->content_h = h;
    return ZCSR_OK;
}

ZCSR_Result zcsr_scroll_view_set_offset(ZCSR_UI* ui, ZCSR_NodeId id, float x, float y)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->scroll_x = x;
    nd->scroll_y = y;
    return ZCSR_OK;
}

ZCSR_Result zcsr_scroll_view_get_offset(const ZCSR_UI* ui, ZCSR_NodeId id, float* x, float* y)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !x || !y) return ZCSR_ERR_NOT_FOUND;
    *x = nd->scroll_x;
    *y = nd->scroll_y;
    return ZCSR_OK;
}

ZCSR_Result zcsr_scroll_view_set_scroll_axes(ZCSR_UI* ui, ZCSR_NodeId id, bool h, bool v)
{
    (void)ui;(void)id;(void)h;(void)v; return ZCSR_OK;
}

ZCSR_Result zcsr_scroll_view_set_scrollbar_visibility(ZCSR_UI* ui, ZCSR_NodeId id, bool h, bool v)
{
    (void)ui;(void)id;(void)h;(void)v; return ZCSR_OK;
}

ZCSR_Result zcsr_scroll_view_set_scrollbar_thickness(ZCSR_UI* ui, ZCSR_NodeId id, float t)
{
    (void)ui;(void)id;(void)t; return ZCSR_OK;
}

ZCSR_Result zcsr_scroll_view_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool v)
{
    return zcsr_button_set_enabled(ui, id, v);
}
