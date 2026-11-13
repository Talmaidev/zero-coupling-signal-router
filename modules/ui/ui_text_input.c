/* modules/ui/ui_text_input.c — TextInput component. */
#include "ui_internal.h"
#include "zcsr/diagnostics.h"

ZCSR_Result zcsr_text_input_set_text(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    char* new_buf;
    if (!nd) {
        ZCSR_LOG_ERROR("ui_text_input", "set_text: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }
    if (!t && n > 0) {
        ZCSR_LOG_ERROR("ui_text_input", "set_text: NULL text with non-zero length");
        return ZCSR_ERR_INVALID_ARGUMENT;
    }

    /* Allocate new buffer first, then free old one (fail-safe) */
    new_buf = (char*)malloc(n + 1);
    if (!new_buf) {
        ZCSR_LOG_ERROR("ui_text_input", "set_text: malloc failed for %u bytes", n + 1);
        return ZCSR_ERR_OUT_OF_MEMORY;
    }

    /* Copy data */
    if (n > 0 && t) memcpy(new_buf, t, n);
    new_buf[n] = '\0';

    /* Free old buffer and swap */
    if (nd->text_buf) free(nd->text_buf);
    nd->text_buf = new_buf;
    nd->text_cap = n + 1;
    nd->caret_byte = n;

    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_get_text(const ZCSR_UI* ui, ZCSR_NodeId id,
                                     char* buf, uint32_t cap, uint32_t* req)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (!nd->text_buf) { if (req) *req = 0; if (buf && cap > 0) buf[0] = '\0'; return ZCSR_OK; }
    uint32_t len = (uint32_t)strlen(nd->text_buf);
    if (req) *req = len;
    if (buf && cap > 0) {
        uint32_t copy = len < cap - 1 ? len : cap - 1;
        memcpy(buf, nd->text_buf, copy);
        buf[copy] = '\0';
    }
    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_max_bytes(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t m)
{
    (void)ui;(void)id;(void)m; return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_read_only(ZCSR_UI* ui, ZCSR_NodeId id, bool r)
{
    (void)ui;(void)id;(void)r; return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_placeholder(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    (void)ui;(void)id;(void)t;(void)n; return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_cursor(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t c)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->caret_byte = c;
    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_get_cursor(const ZCSR_UI* ui, ZCSR_NodeId id, uint32_t* out)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd || !out) return ZCSR_ERR_NOT_FOUND;
    *out = nd->caret_byte;
    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_selection(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t s, uint32_t e)
{
    (void)ui;(void)id;(void)s;(void)e; return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool v)
{
    return zcsr_button_set_enabled(ui, id, v);
}
