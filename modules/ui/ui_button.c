/* modules/ui/ui_button.c — Button component setters. */
#include "ui_internal.h"
#include "zcsr/diagnostics.h"

ZCSR_Result zcsr_button_set_text(ZCSR_UI* ui, ZCSR_NodeId id, const char* t, uint32_t n)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    char* new_buf;
    if (!nd) {
        ZCSR_LOG_ERROR("ui_button", "set_text: node not found: %u", id);
        return ZCSR_ERR_NOT_FOUND;
    }
    if (nd->component != ZCSR_COMPONENT_BUTTON) {
        ZCSR_LOG_ERROR("ui_button", "set_text: node %u is not a button", id);
        return ZCSR_ERR_TYPE_MISMATCH;
    }
    if (!t && n > 0) {
        ZCSR_LOG_ERROR("ui_button", "set_text: NULL text with non-zero length");
        return ZCSR_ERR_INVALID_ARGUMENT;
    }

    /* Allocate new buffer first, then free old one (fail-safe) */
    new_buf = (char*)malloc(n + 1);
    if (!new_buf) {
        ZCSR_LOG_ERROR("ui_button", "set_text: malloc failed for %u bytes", n + 1);
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

/* 注：原 zcsr_button_set_colors(normal/hover/pressed/disabled) 已废弃并删除。
 * 四态配色是组件语义（状态机 + 视觉主题），属于 UIM 的组件属性域；
 * 引擎 node 只有单组 bg/text/border + override 标志，无实现载体。
 * 留一个 (void)arg; return ZCSR_OK 的空 setter 就是假 API，所以直接删。 */

ZCSR_Result zcsr_button_set_enabled(ZCSR_UI* ui, ZCSR_NodeId id, bool e)
{
    ZCSR_UI_Node* nd = ui_find_node(ui, id);
    if (!nd) return ZCSR_ERR_NOT_FOUND;
    nd->enabled = e;
    if (e) nd->flags |= ZCSR_NODE_ENABLED; else nd->flags &= ~ZCSR_NODE_ENABLED;
    return ZCSR_OK;
}
