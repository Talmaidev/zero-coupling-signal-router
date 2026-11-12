/* modules/ui/ui_image.c - Image component.
 *
 * 这 4 个 API 以前全是 (void)arg; return ZCSR_OK; —— 调用方以为设置了，实际什么都没存，
 * 于是 Image 永远画不出图。现在真的写进节点状态，由 draw list 生成 ZCSR_DRAW_IMAGE，
 * 再由 GL 后端按 resource_key 查宿主纹理表。
 *
 * 边界：fit_mode 只记录意图。contain/cover 需要纹理像素尺寸，而 ZCSR_DrawCommand
 * 当前不带尺寸（宿主资源表也没给），所以 draw 端仍按 stretch 铺满 rect。
 * 等资源表补上 w/h 后再在 draw 端实现 contain/cover，不在这里假装做过。
 */
#include "ui_internal.h"

ZCSR_Result zcsr_image_set_resource(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t k)
{
    ZCSR_UI_Node* n = ui_find_node(ui, id);
    if (n == NULL) return ZCSR_ERR_NOT_FOUND;
    if (n->component != ZCSR_COMPONENT_IMAGE) return ZCSR_ERR_TYPE_MISMATCH;
    n->resource_key = k;
    return ZCSR_OK;
}

ZCSR_Result zcsr_image_set_uv(ZCSR_UI* ui, ZCSR_NodeId id, const ZCSR_Rect* r)
{
    ZCSR_UI_Node* n = ui_find_node(ui, id);
    if (n == NULL) return ZCSR_ERR_NOT_FOUND;
    if (r == NULL) return ZCSR_ERR_NULL_POINTER;
    if (n->component != ZCSR_COMPONENT_IMAGE) return ZCSR_ERR_TYPE_MISMATCH;
    if (r->width <= 0.0f || r->height <= 0.0f) return ZCSR_ERR_INVALID_ARGUMENT;
    n->uv_x = r->x;
    n->uv_y = r->y;
    n->uv_w = r->width;
    n->uv_h = r->height;
    return ZCSR_OK;
}

ZCSR_Result zcsr_image_set_tint(ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_Color c)
{
    return zcsr_node_set_color(ui, id, ZCSR_COLOR_BG, c);
}

ZCSR_Result zcsr_image_set_fit_mode(ZCSR_UI* ui, ZCSR_NodeId id, int32_t m)
{
    ZCSR_UI_Node* n = ui_find_node(ui, id);
    if (n == NULL) return ZCSR_ERR_NOT_FOUND;
    if (n->component != ZCSR_COMPONENT_IMAGE) return ZCSR_ERR_TYPE_MISMATCH;
    if (m < 0 || m > 2) return ZCSR_ERR_INVALID_ARGUMENT;
    n->fit_mode = m;
    return ZCSR_OK;
}
