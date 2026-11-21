/* modules/ui/ui_label.c — Label/Text component. */
#include "ui_internal.h"
#include "zcsr/diagnostics.h"

ZCSR_Result zcsr_label_set_text(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) {
        ZCSR_LOG_ERROR("ui_label", "set_text: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }
    if (!t && n > 0) {
        ZCSR_LOG_ERROR("ui_label", "set_text: NULL text with non-zero length");
        return ZCSR_ERR_INVALID_ARGUMENT;
    }
    if (n >= nd->text_cap) {
        ZCSR_LOG_ERROR("ui_label", "set_text: text too long: %u >= %u", n, nd->text_cap);
        return ZCSR_ERR_BUFFER_TOO_SMALL;
    }

    if (n > 0 && t) memcpy(nd->text_buf, t, n);
    nd->text_buf[n] = '\0';
    return ZCSR_OK;
}

ZCSR_Result zcsr_label_set_color(ZCSR_UI* ui, ZCSR_NodeId id, ZCSR_Color c)
{
    return zcsr_node_set_color(ui, id, ZCSR_COLOR_TEXT, c);
}

ZCSR_Result zcsr_label_set_alignment(ZCSR_UI* ui, ZCSR_NodeId id, int32_t h, int32_t v)
{
    (void)ui;(void)id;(void)h;(void)v; return ZCSR_OK;
}
