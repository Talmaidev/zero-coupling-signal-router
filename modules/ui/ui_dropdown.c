/* modules/ui/ui_dropdown.c — Dropdown/Select component. */
#include "ui_internal.h"

ZCSR_Result zcsr_dropdown_clear_items(ZCSR_UI* ui, ZCSR_NodeId id)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->item_count = 0;
    nd->int_val = -1;
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_add_item(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (nd->item_count >= ZCSR_DROPDOWN_MAX_ITEMS) return ZCSR_ERR_BUFFER_TOO_SMALL;
    if (n >= ZCSR_DROPDOWN_MAX_TEXT) n = ZCSR_DROPDOWN_MAX_TEXT - 1;
    memcpy(nd->items[nd->item_count], t, n);
    nd->items[nd->item_count][n] = '\0';
    nd->item_count++;
    if (nd->int_val < 0) nd->int_val = 0;
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_set_selected(ZCSR_UI* ui, ZCSR_NodeId id, int32_t v)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (v < 0 || v >= (int32_t)nd->item_count) return ZCSR_ERR_INVALID_ARGUMENT;
    nd->int_val = v;
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_get_selected(const ZCSR_UI* ui, ZCSR_NodeId id, int32_t* out)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !out) return ZCSR_ERR_NOT_FOUND;
    *out = nd->int_val;
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_get_item_count(const ZCSR_UI* ui, ZCSR_NodeId id, uint32_t* out)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !out) return ZCSR_ERR_NOT_FOUND;
    *out = nd->item_count;
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_get_item_text(const ZCSR_UI* ui, ZCSR_NodeId id, uint32_t idx,
                                        const char** out_text, uint32_t* out_len)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !out_text || !out_len) return ZCSR_ERR_NOT_FOUND;
    if (idx >= nd->item_count) return ZCSR_ERR_INVALID_ARGUMENT;
    *out_text = nd->items[idx];
    *out_len = (uint32_t)strlen(nd->items[idx]);
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_set_max_popup_height(ZCSR_UI* ui, ZCSR_NodeId id, float h)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->popup_max_h = h;
    return ZCSR_OK;
}

ZCSR_Result zcsr_dropdown_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool e)
{
    return zcsr_button_set_enabled(ui, id, e);
}
