/* modules/ui/ui_text_input.c — TextInput accessors.
 *
 * This file owns no editing logic. Caret movement, selection, UTF-8
 * boundaries and the length limit all live in zcsr/ui_text_edit.h, which UIM
 * includes too; here we only bind the node buffer to that view and map its
 * status codes onto ZCSR_Result.
 */
#include "ui_internal.h"
#include "zcsr/ui_text_edit.h"
#include "zcsr/diagnostics.h"

static ZCSR_Result text_status_to_result(zcsr_text_status st)
{
    switch (st) {
    case ZCSR_TEXT_OK:            return ZCSR_OK;
    case ZCSR_TEXT_ERR_ARGUMENT:  return ZCSR_ERR_INVALID_ARGUMENT;
    case ZCSR_TEXT_ERR_RANGE:     return ZCSR_ERR_INVALID_ARGUMENT;
    case ZCSR_TEXT_ERR_BUFFER:    return ZCSR_ERR_BUFFER_TOO_SMALL;
    case ZCSR_TEXT_ERR_READ_ONLY: return ZCSR_ERR_INVALID_STATE;
    default:                      return ZCSR_ERR_INVALID_STATE;
    }
}

static zcsr_text_edit bind_node_text(ZCSR_UI_Node* nd)
{
    zcsr_text_edit e;
    e.data = nd->text_buf;
    e.capacity = nd->text_cap > 0 ? (size_t)nd->text_cap - 1u : 0u;
    e.length = strlen(nd->text_buf);
    e.cursor = nd->caret_byte <= e.length ? nd->caret_byte : e.length;
    e.anchor = nd->text_anchor <= e.length ? nd->text_anchor : e.cursor;
    e.read_only = nd->read_only;
    return e;
}

static void store_node_text(ZCSR_UI_Node* nd, const zcsr_text_edit* e)
{
    nd->caret_byte = (uint32_t)e->cursor;
    nd->text_anchor = (uint32_t)e->anchor;
}

ZCSR_Result zcsr_text_input_set_text(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    zcsr_text_edit e;
    zcsr_text_status st;
    if (!nd) {
        ZCSR_LOG_ERROR("ui_text_input", "set_text: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }
    if (!t && n > 0) {
        ZCSR_LOG_ERROR("ui_text_input", "set_text: NULL text with non-zero length");
        return ZCSR_ERR_INVALID_ARGUMENT;
    }
    e = bind_node_text(nd);
    st = zcsr_text_set(&e, t, n);
    if (st == ZCSR_TEXT_ERR_BUFFER)
        ZCSR_LOG_ERROR("ui_text_input", "set_text: text too long: %u > %u",
                       n, (uint32_t)e.capacity);
    if (st != ZCSR_TEXT_OK) return text_status_to_result(st);
    store_node_text(nd, &e);
    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_get_text(const ZCSR_UI* ui, ZCSR_NodeId id,
                                     char* buf, uint32_t cap, uint32_t* req)
{
    const ZCSR_UI_Node* nd = ui_find_node_c(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (nd->text_cap == 0 || nd->text_buf[0] == '\0') {
        if (req) *req = 0;
        if (buf && cap > 0) buf[0] = '\0';
        return ZCSR_OK;
    }
    uint32_t len = (uint32_t)strlen(nd->text_buf);
    if (req) *req = len;
    if (buf && cap > 0) {
        uint32_t copy = len < cap - 1 ? len : cap - 1;
        memcpy(buf, nd->text_buf, copy);
        buf[copy] = '\0';
    }
    return ZCSR_OK;
}

/* m is the max payload bytes, 1..ZCSR_UI_TEXT_CAPACITY-1. */
ZCSR_Result zcsr_text_input_set_max_bytes(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t m)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    if (m == 0 || m >= ZCSR_UI_TEXT_CAPACITY) return ZCSR_ERR_INVALID_ARGUMENT;
    if ((uint32_t)strlen(nd->text_buf) > m) return ZCSR_ERR_BUFFER_TOO_SMALL;
    nd->text_cap = m + 1u;   /* text_cap stays array-size; keep room for the NUL */
    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_read_only(ZCSR_UI* ui, ZCSR_NodeId id, bool r)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->read_only = r;
    return ZCSR_OK;
}

/* Placeholder text needs a draw path (show it only while the field is empty).
 * There is none yet, so fail honestly instead of pretending to accept it. */
ZCSR_Result zcsr_text_input_set_placeholder(ZCSR_UI* ui, ZCSR_NodeId id,
                                            const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    (void)t; (void)n;
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    ZCSR_LOG_ERROR("ui_text_input", "set_placeholder: no draw path, unsupported");
    return ZCSR_ERR_UNSUPPORTED;
}

ZCSR_Result zcsr_text_input_set_cursor(ZCSR_UI* ui, ZCSR_NodeId id, uint32_t c)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    zcsr_text_edit e;
    zcsr_text_status st;
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    e = bind_node_text(nd);
    st = zcsr_text_set_cursor(&e, c);
    if (st != ZCSR_TEXT_OK) return text_status_to_result(st);
    store_node_text(nd, &e);
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
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    zcsr_text_edit view;
    zcsr_text_status st;
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    view = bind_node_text(nd);
    st = zcsr_text_set_selection(&view, s, e);
    if (st != ZCSR_TEXT_OK) return text_status_to_result(st);
    store_node_text(nd, &view);
    return ZCSR_OK;
}

ZCSR_Result zcsr_text_input_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool v)
{
    return zcsr_button_set_enabled(ui, id, v);
}
