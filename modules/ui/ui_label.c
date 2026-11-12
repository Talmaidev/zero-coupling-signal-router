/* modules/ui/ui_label.c — Label/Text component. */
#include "ui_internal.h"
#include "zcsr/diagnostics.h"

ZCSR_Result zcsr_label_set_text(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    char* new_buf;
    if (!nd) {
        ZCSR_LOG_ERROR("ui_label", "set_text: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }
    if (!t && n > 0) {
        ZCSR_LOG_ERROR("ui_label", "set_text: NULL text with non-zero length");
        return ZCSR_ERR_INVALID_ARGUMENT;
    }

    /* Allocate new buffer first, then free old one (fail-safe) */
    new_buf = (char*)malloc(n + 1);
    if (!new_buf) {
        ZCSR_LOG_ERROR("ui_label", "set_text: malloc failed for %u bytes", n + 1);
        return ZCSR_ERR_OUT_OF_MEMORY;
    }

    /* Copy data */
    if (n > 0 && t) memcpy(new_buf, t, n);
    new_buf[n] = '\0';

    /* Free old buffer and swap */
    if (nd->text_buf) free(nd->text_buf);
    nd->text_buf = new_buf;
    nd->text_cap = n + 1;

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
