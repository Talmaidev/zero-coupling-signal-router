#ifndef ZCSR_UI_TOGGLE_H
#define ZCSR_UI_TOGGLE_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_toggle_set_value(ZCSR_UI*, ZCSR_NodeId, bool);
ZCSR_Result zcsr_toggle_get_value(const ZCSR_UI*, ZCSR_NodeId, bool*);
ZCSR_Result zcsr_toggle_set_text(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
ZCSR_Result zcsr_toggle_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
