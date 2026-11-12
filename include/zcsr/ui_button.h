#ifndef ZCSR_UI_BUTTON_H
#define ZCSR_UI_BUTTON_H
#include "zcsr/ui_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZCSR_UI ZCSR_UI;
ZCSR_Result zcsr_button_set_text(ZCSR_UI*, ZCSR_NodeId, const char*, uint32_t);
ZCSR_Result zcsr_button_set_colors(ZCSR_UI*, ZCSR_NodeId, ZCSR_Color, ZCSR_Color, ZCSR_Color, ZCSR_Color);
ZCSR_Result zcsr_button_set_enabled(ZCSR_UI*, ZCSR_NodeId, bool);
#ifdef __cplusplus
}
#endif
#endif
