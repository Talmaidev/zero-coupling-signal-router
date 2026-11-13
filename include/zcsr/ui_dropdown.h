#ifndef ZCSR_UI_DROPDOWN_H
#define ZCSR_UI_DROPDOWN_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_dropdown_clear_items(ZCSR_UI*, ZCSR_NodeId);
ZCSR_Result zcsr_dropdown_add_item(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
ZCSR_Result zcsr_dropdown_set_selected(ZCSR_UI*, ZCSR_NodeId, int32_t);
ZCSR_Result zcsr_dropdown_get_selected(const ZCSR_UI*, ZCSR_NodeId, int32_t*);
ZCSR_Result zcsr_dropdown_get_item_count(const ZCSR_UI*, ZCSR_NodeId, uint32_t*);
ZCSR_Result zcsr_dropdown_get_item_text(const ZCSR_UI*, ZCSR_NodeId, uint32_t,
                                        const char**, uint32_t*);
ZCSR_Result zcsr_dropdown_set_max_popup_height(ZCSR_UI*, ZCSR_NodeId, float);
ZCSR_Result zcsr_dropdown_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
