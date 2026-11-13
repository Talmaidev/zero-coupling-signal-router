/* modules/ui/ui_slider.c — Slider component. */
#include "ui_internal.h"

ZCSR_Result zcsr_slider_set_range(ZCSR_UI* ui, ZCSR_NodeId id, float mn, float mx)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (mn >= mx) return ZCSR_ERR_INVALID_ARGUMENT;
    nd->float_min = mn; nd->float_max = mx;
    return ZCSR_OK;
}

ZCSR_Result zcsr_slider_set_step(ZCSR_UI* ui, ZCSR_NodeId id, float s)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->float_step = s;
    return ZCSR_OK;
}

ZCSR_Result zcsr_slider_set_value(ZCSR_UI* ui, ZCSR_NodeId id, float v)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (v < nd->float_min) v = nd->float_min;
    if (v > nd->float_max) v = nd->float_max;
    nd->float_val = v;
    return ZCSR_OK;
}

ZCSR_Result zcsr_slider_get_value(const ZCSR_UI* ui, ZCSR_NodeId id, float* out)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !out) return ZCSR_ERR_NOT_FOUND;
    *out = nd->float_val;
    return ZCSR_OK;
}

ZCSR_Result zcsr_slider_set_orientation(ZCSR_UI* ui, ZCSR_NodeId id, bool v)
{
    (void)ui;(void)id;(void)v; return ZCSR_OK;
}

ZCSR_Result zcsr_slider_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool e)
{
    return zcsr_button_set_enabled(ui, id, e);
}
