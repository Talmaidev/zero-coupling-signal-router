/* modules/ui/ui_separator.c — Separator/Divider component. */
#include "ui_internal.h"

ZCSR_Result zcsr_separator_set_orientation(ZCSR_UI* ui, ZCSR_NodeId id, bool v)
{
    (void)ui;(void)id;(void)v; return ZCSR_OK;
}

ZCSR_Result zcsr_separator_set_color(ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_Color c)
{
    return zcsr_node_set_color(ui, id, ZCSR_COLOR_BG, c);
}

ZCSR_Result zcsr_separator_set_thickness(ZCSR_UI* ui, ZCSR_NodeId id, float t)
{
    (void)ui;(void)id;(void)t; return ZCSR_OK;
}
