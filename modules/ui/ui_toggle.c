/* modules/ui/ui_toggle.c — Toggle/Switch component. */
#include "ui_internal.h"

ZCSR_Result zcsr_toggle_set_value(ZCSR_UI* ui, ZCSR_NodeId id, bool v)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->bool_val = v;
    return ZCSR_OK;
}

ZCSR_Result zcsr_toggle_get_value(const ZCSR_UI* ui, ZCSR_NodeId id, bool* out)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !out) return ZCSR_ERR_NOT_FOUND;
    *out = nd->bool_val;
    return ZCSR_OK;
}

ZCSR_Result zcsr_toggle_set_text(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    return zcsr_button_set_text(ui, id, t, n);
}

ZCSR_Result zcsr_toggle_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool e)
{
    return zcsr_button_set_enabled(ui, id, e);
}
